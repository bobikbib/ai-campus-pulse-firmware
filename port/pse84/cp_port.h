/*
 * AI Campus Pulse — the only header the Infineon example needs to include.
 */
#ifndef CP_PORT_H
#define CP_PORT_H

#include <stdint.h>
#include <stdbool.h>

/* Start the Campus Pulse task. Call once before vTaskStartScheduler(). */
bool cp_port_start(void);

/* Call from the inference loop after every frame with the number of people
 * (aihub: YOLACT instances, face-id: prediction.count). Never blocks. */
void cp_port_on_detections(uint16_t num_people);

#endif /* CP_PORT_H */
