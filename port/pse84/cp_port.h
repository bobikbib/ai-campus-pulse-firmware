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

/* Microphone: call with every block of 16 kHz 16-bit mono PCM from the PDM
 * driver (any block size). Must be called from a task, not from an ISR.
 * Only two numbers per 100 ms leave this function (level, voice); the samples
 * are not stored or sent anywhere. Never blocks. */
void cp_port_on_audio(const int16_t *pcm, uint32_t n);

/* Camera duty cycle: call from the inference loop right after
 * cp_port_on_detections(). Returns at once while the camera is active; while
 * it is asleep it blocks (in 50 ms sleeps) until the next sample frame is due
 * or something wakes the camera (sound, NFC scan). So the camera and the AI
 * model only run when needed. */
void cp_port_frame_gate(void);

#endif /* CP_PORT_H */
