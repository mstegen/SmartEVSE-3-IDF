/*
 * SmartEVSE-3 v3 — app_main() entry point for native ESP-IDF v6.0.1
 *
 * The original Arduino firmware defined `setup()` and `loop()`. Under native
 * ESP-IDF, the entry point is `app_main()`. We declare `setup()` and `loop()`
 * as extern (they live in src/main.cpp) and call setup() once. The real
 * application work runs in three FreeRTOS tasks created inside setup()
 * (Timer10ms / Timer100ms / Timer1S) plus the Mongoose poll task created in
 * network_setup(). Therefore the main task has nothing useful to do and is
 * deleted; the system keeps running on the worker tasks.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif
void setup(void);
void loop(void);
#ifdef __cplusplus
}
#endif

void app_main(void)
{
    /* Call the legacy Arduino setup() once. After that, all real work runs
     * inside FreeRTOS tasks that setup() itself creates. */
    setup();

    /* The original Arduino loop() body is now empty by design (work is in
     * tasks), but call it once anyway in case future code adds work there,
     * and then keep the main task alive. */
    for (;;) {
        loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
