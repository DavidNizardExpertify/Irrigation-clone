#include "timeSystem.h"

#include <cstdio>
#include <ctime>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "apps/sntp/sntp.h"

#include "wifiEvents.h"


// ********************************************************************
// private objects, vars and prototypes
// ********************************************************************
static const char* LOG_TAG_TIME = "time";

static EventGroupHandle_t timeEvents;
const int timeEventTimeSet = (1<<0);

TaskHandle_t sntpTaskHandle;
void sntp_task(void* params);


// ********************************************************************
// time system initialization
// ********************************************************************
extern "C" void TimeSystem_Init(void)
{
    time_t now;
    struct tm timeinfo;

    timeEvents = xEventGroupCreate();

    ESP_LOGI(LOG_TAG_TIME, "Checking if time is already set.");
    time(&now);

    // set correct timezone
    setenv("TZ", "CET-1CEST", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if(!(timeinfo.tm_year < (2017 - 1900))) {
        ESP_LOGI(LOG_TAG_TIME, "-> Time already set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, timeEventTimeSet);
        TimeSystem_LogTime();
    } else {
        ESP_LOGI(LOG_TAG_TIME, "-> Time not set.");
    }

    if(pdPASS == xTaskCreate(sntp_task, "sntp_task", 2048, (void*) NULL, tskIDLE_PRIORITY+1, &sntpTaskHandle)) {
        ESP_LOGI(LOG_TAG_TIME, "SNTP task created. Starting.");
    } else {
        ESP_LOGE(LOG_TAG_TIME, "SNTP task creation failed!");
    }
}

// ********************************************************************
// time getters and setters
// ********************************************************************
extern "C" void TimeSystem_GetCurTimeStr(char *timeStr)
{
    if(NULL == timeStr) return;

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(timeStr, 20, "%d.%m.%Y %H:%M:%S", &timeinfo);
}

extern "C" int TimeSystem_SetTime(int16_t day, int16_t month, int16_t year, int16_t hour, int16_t minute, int16_t second)
{
    int result = -1;

    struct timeval tv;
    struct tm tm;
    time_t t;

    if((day > 31) || (day < 1)) return -1;
    if((month > 12) || (month < 1)) return -1;
    if((year < 1970)) return -1;
    if((hour > 23) || (hour < 0)) return -1;
    if((minute > 59) || (minute < 0)) return -1;
    if((second > 59) || (second < 0)) return -1;

    tm.tm_mday = day;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;

    t = mktime(&tm);
    tv.tv_sec = t;
    tv.tv_usec = 0;

    result = settimeofday(&tv, NULL);

    if(0 == result) {
        ESP_LOGI(LOG_TAG_TIME, "Time set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, timeEventTimeSet);
        TimeSystem_LogTime();
    } else {
        ESP_LOGE(LOG_TAG_TIME, "settimeofday failed with exit code %d.", result);
    }

    return result;
}

// ********************************************************************
// misc helpers
// ********************************************************************
bool TimeSystem_TimeIsSet(void)
{
    return (0 != (xEventGroupGetBits(timeEvents) & timeEventTimeSet));
}

void TimeSystem_LogTime(void)
{
    static char strftime_buf[64];
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(LOG_TAG_TIME, "Current time: %s", strftime_buf);
}

// ********************************************************************
// SNTP handling
// ********************************************************************
void sntp_task(void* params)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    while(1) {
        // wait being online
        xEventGroupWaitBits(wifiEvents, wifiEventConnected, false, true, portMAX_DELAY);

        ESP_LOGI(LOG_TAG_TIME, "WiFi connect detected. Initializing SNTP.");
        TimeSystem_SntpStart();

        while(timeinfo.tm_year < (2017 - 1900)) {
            ESP_LOGI(LOG_TAG_TIME, "Waiting for system time to be set.");
            vTaskDelay(pdMS_TO_TICKS(2000));
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        ESP_LOGI(LOG_TAG_TIME, "Time set. Setting timeEvents.");
        xEventGroupSetBits(timeEvents, timeEventTimeSet);

        TimeSystem_LogTime();

        // wait for potential connection loss
        xEventGroupWaitBits(wifiEvents, wifiEventDisconnected, false, true, portMAX_DELAY);

        ESP_LOGI(LOG_TAG_TIME, "WiFi disconnect detected. Stopping SNTP.");
        TimeSystem_SntpStop();
    }

    vTaskDelete(NULL);
}

void TimeSystem_SntpStart(void)
{
    if(0 != (xEventGroupGetBits(wifiEvents) & wifiEventConnected)) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, (char*) "de.pool.ntp.org");
        sntp_init();
    }
}

void TimeSystem_SntpStop(void)
{
    if(sntp_enabled()) {
        sntp_stop();
    }
}

void TimeSystem_SntpRequest(void)
{
    TimeSystem_SntpStop();
    TimeSystem_SntpStart();
}