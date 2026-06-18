#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_i2c.h"
#include "inc/hw_gpio.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/interrupt.h"
#include "driverlib/uart.h"
#include "driverlib/timer.h"
#include "driverlib/i2c.h"
#include "driverlib/pwm.h"
#include "driverlib/hibernate.h"
#include "board_config.h"
#include "app_state.h"
#include "uart_port.h"
#include "i2c_bus.h"
#include "pwm_buzzer.h"
#include "hibernate_rtc.h"
#include "datetime.h"
#include "boot_sequence.h"
#include "display.h"

// 运行启动初始化显示序列，包括RTC校准和学生信息显示
void RunInitializationSequence(void)
{
    uint8_t max_days_for_current_month;
    uint32_t pastSec_local_in_init;

    currentRTC = HibernateRTCGet(); // 获取当前RTC值
    HibernateDataGet(fetchRTC, 4);  // 获取休眠模块存储的数据

    // 如果有历史RTC数据且当前RTC大于上次存储的RTC，则根据时间差校准时钟
    if (fetchRTC[3] != 0 && currentRTC >= fetchRTC[3])
    {
        pastSec_local_in_init = currentRTC - fetchRTC[3]; // 计算休眠期间经过的秒数
        hh = (int8_t)fetchRTC[0];                         // 恢复小时
        mm = (int8_t)fetchRTC[1];                         // 恢复分钟
        ss = (int8_t)fetchRTC[2];                         // 恢复秒

        ss = (int8_t)(ss + pastSec_local_in_init); // 增加经过的秒数
        while (ss >= 60)                           // 处理秒溢出
        {
            mm++;
            ss -= 60;
        }
        while (mm >= 60) // 处理分钟溢出
        {
            hh++;
            mm -= 60;
        }
        while (hh >= 24) // 处理小时溢出和日期更新
        {
            day++;
            hh = 0;

            max_days_for_current_month = days_in_month[month];
            if (month == 2 && is_leap_year(year))
            {
                max_days_for_current_month = 29;
            }
            if (day > max_days_for_current_month) // 处理日期溢出
            {
                day = 1;
                month++;
                if (month > 12) // 处理月份溢出
                {
                    month = 1;
                    year++;
                }
            }
        }
    }
    else // 首次启动或RTC数据无效，设置默认时间
    {
        hh = 0;
        mm = 0;
        ss = 0;
        year = 2025;
        month = 6;
        day = 3;
        alm_hh = 25;
        alm_mm = 0;
        alm_ss = 0;
    }

    if (clock2ms_flag == true) // 每2ms刷新一次显示
    {
        clock2ms_flag = false;
        switch (init_procedure) // 根据初始化步骤显示不同内容
        {
        case 0:
            // 显示学生ID的一个字符，并关闭LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, stuID[cnt]);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
            break;
        case 1:
            // 清除数码管显示，并打开LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xff);
            break;
        case 2:
            // 显示名字的一个字符，并关闭LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, name[cnt]);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);
            break;
        case 3:
            // 清除数码管显示，并打开LED
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, 0x00);
            result = I2C0_WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, rightshift);
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xff);
            break;
        default:
            // 初始化完成，进入正常模式
            init_flag = false;
            shift = 0;
            rightshift = 0x01;
            cnt = 0;
            result = I2C0_WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0x00);

            // 保存当前时间为原始值，用于后续回滚
            original_year = year;
            original_month = month;
            original_day = day;
            original_hh = hh;
            original_mm = mm;
            original_ss = ss;
            original_alm_hh = alm_hh;
            original_alm_mm = alm_mm;
            original_alm_ss = alm_ss;
            unsaved_changes_active = false;
            seven_segment_display_on = true;
            break;
        }

        cnt++;                        // 切换到下一个数码管
        rightshift = rightshift << 1; // 移位位选
        if (cnt >= 0x8)               // 8个数码管循环
        {
            rightshift = 0x01;
            cnt = 0;
        }
    }

    if (clock900ms_flag == true) // 每900ms切换初始化步骤
    {
        clock900ms_flag = false;
        init_procedure++;
    }
}

// 遍历并处理所有按钮的事件
