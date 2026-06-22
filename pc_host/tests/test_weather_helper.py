import sys
import threading
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import weather_helper as module
from PyQt5.QtCore import QCoreApplication, QEventLoop, QTimer
from weather_helper import WeatherHelper, WeatherProviderError, WeatherSnapshot


class FakeSerialWorker:
    def __init__(self):
        self.lines = []

    def send_line(self, line):
        self.lines.append(line)


class FakeResponse:
    def __init__(self, payload):
        self.payload = payload

    def raise_for_status(self):
        return None

    def json(self):
        return self.payload


class WeatherHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QCoreApplication.instance() or QCoreApplication([])

    def setUp(self):
        self.serial = FakeSerialWorker()
        self.helper = WeatherHelper(self.serial)
        self.commands = []
        self.helper.command_ready.connect(self.commands.append)

    def test_open_meteo_mapping_and_led_encoding(self):
        snapshot = self.helper._parse_open_meteo({
            "current": {"temperature_2m": 36.6, "weather_code": 61},
        })
        self.helper._apply_snapshot(snapshot)

        self.assertEqual(snapshot.temperature_c, 37)
        self.assertEqual(snapshot.condition, "RAIN")
        self.assertEqual(snapshot.provider, "Open-Meteo")
        self.assertEqual(self.helper._compute_weather_led(), 0x60)

    def test_proxy_tls_failure_retries_without_environment_proxy(self):
        assert module.requests is not None
        original_session = module.requests.Session
        calls = []

        class FakeSession:
            trust_env = True

            def get(inner_self, *args, **kwargs):
                calls.append(inner_self.trust_env)
                if inner_self.trust_env:
                    raise module.requests.exceptions.ProxyError("broken proxy")
                return FakeResponse({
                    "current": {"temperature_2m": 25, "weather_code": 0},
                })

            def close(inner_self):
                pass

        module.requests.Session = FakeSession
        try:
            snapshot = self.helper._fetch_open_meteo()
        finally:
            module.requests.Session = original_session

        self.assertEqual(calls, [True, False])
        self.assertEqual(snapshot.condition, "SUNNY")

    def test_wttr_is_used_when_primary_provider_fails(self):
        self.helper._fetch_open_meteo = lambda: self._raise_provider_error()
        self.helper._fetch_wttr = lambda: WeatherSnapshot(
            21, "CLOUDY", "多云", "wttr.in"
        )

        ok, message = self.helper.fetch_now()

        self.assertTrue(ok)
        self.assertIn("wttr.in", message)
        self.assertEqual(self.helper.snapshot.condition, "CLOUDY")
        self.assertEqual(self.commands, [])

    def test_recent_cache_survives_provider_outage(self):
        self.helper._apply_snapshot(WeatherSnapshot(18, "SUNNY", "晴", "cache"))
        self.helper._fetch_open_meteo = lambda: self._raise_provider_error()
        self.helper._fetch_wttr = lambda: self._raise_provider_error()

        ok, message = self.helper.fetch_now()

        self.assertTrue(ok)
        self.assertIn("缓存", message)

    def test_user2_message_is_ascii_and_led_is_sent(self):
        self.helper._apply_snapshot(WeatherSnapshot(-3, "SNOW", "雪", "test"))

        self.assertTrue(self.helper.send_weather_to_mcu())
        self.assertEqual(
            self.commands,
            ["*SET:MSG -3C SNOW", "*SET:LED 20"],
        )

    def test_user2_cloudy_message_fits_static_eight_digit_display(self):
        snapshot = WeatherSnapshot(26, "CLOUDY", "多云", "test")

        self.assertEqual(snapshot.mcu_message, "26C CLD")
        self.assertLessEqual(len(snapshot.mcu_message), 8)
        self.assertEqual(
            WeatherSnapshot(-99, "RAIN", "雨", "test").mcu_message,
            "-99CRAIN",
        )

    def test_async_fetch_emits_led_and_restores_busy_state(self):
        entered = threading.Event()
        release = threading.Event()

        def fake_fetch():
            entered.set()
            release.wait(1.0)
            self.helper._apply_snapshot(
                WeatherSnapshot(28, "SUNNY", "晴", "test")
            )
            return True, "28°C 晴 · test"

        self.helper._do_fetch = fake_fetch
        finished = []
        loop = QEventLoop()
        self.helper.fetch_finished.connect(lambda *args: finished.append(args))
        self.helper.fetch_finished.connect(lambda *_: loop.quit())

        self.assertTrue(self.helper.request_fetch("manual"))
        self.assertTrue(entered.wait(1.0))
        self.assertFalse(self.helper.request_fetch("timer"))
        release.set()
        QTimer.singleShot(2000, loop.quit)
        loop.exec_()

        self.assertEqual(self.commands, ["*SET:LED 80"])
        self.assertTrue(finished and finished[0][0])
        self.assertFalse(self.helper.fetching)

    @staticmethod
    def _raise_provider_error():
        raise WeatherProviderError("offline")


if __name__ == "__main__":
    unittest.main()
