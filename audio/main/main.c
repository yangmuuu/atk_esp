#include <stdio.h>
#include "audio.h"
#include "driver/gpio.h"

void app_main(void)
{
	init_speaker(GPIO_NUM_46,GPIO_NUM_9,GPIO_NUM_8,24000);
	init_pdm_microphone(GPIO_NUM_42,GPIO_NUM_3,24000);
}
