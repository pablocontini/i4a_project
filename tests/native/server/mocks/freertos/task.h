#ifndef I4A_SERVER_TEST_FREERTOS_TASK_H
#define I4A_SERVER_TEST_FREERTOS_TASK_H
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *,int,void *,int,TaskHandle_t *,int);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);

#endif
