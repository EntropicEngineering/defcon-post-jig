#include <stdio.h>
#include "ws2812.pio.h"
#include <pico/time.h>

#define IS_RGBW false
#define WS2812_PIN 8

uint vbat_enable_pins[2] = {6, 7}; // ACTIVE = HIGH
uint vbat_sense_pins[2] = {27, 26}; // ANALOG
uint vbus_enable_pins[2] = {0, 3}; // ACTIVE = HIGH
uint vbus_fault_pins[2] = {1, 2}; // FAULT = LOW
uint vbus_sense_pin = 28; // ANALOG
uint post_pins[2] = {4, 5}; // DIGITAL

void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

void write_pixels(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	put_pixel(a);
	put_pixel(b);
	put_pixel(c);
	put_pixel(d);
}

void write_pixels_same(uint32_t a)
{
	write_pixels(a, a, a, a);
}

void init_pin_array(uint* arr, int len, int dir)
{
	for (int i = 0; i < len; i++)
	{
		gpio_init(arr[i]);
		gpio_set_dir(arr[i], dir);

		if (dir == GPIO_OUT)
		{
			gpio_put(arr[i], false);
		}
		else
		{
			gpio_pull_up(arr[i]);
		}
	}
}

void set_vbus(int badgeno, bool val)
{
	gpio_put(vbus_enable_pins[badgeno], val);
}

void set_vbat(int badgeno, bool val)
{
	gpio_put(vbat_enable_pins[badgeno], val);
}

bool get_post(int badgeno)
{
	return gpio_get(post_pins[badgeno]);
}

void set_post(int badgeno, bool value)
{
	gpio_put(post_pins[badgeno], value);
}

bool get_vbus_faulted(int badgeno)
{
	return !gpio_get(vbus_fault_pins[badgeno]);
}

void interactive_post()
{
	bool vbat[2] = {0, 0};
    bool vbus[2] = {0, 0};
    while (true)
    {
    	char c = getchar_timeout_us(100);
    	if (c == 'q')
    		vbus[0] ^= 1;
    	if (c == 'w')
    		vbat[0] ^= 1;
    	if (c == 'r')
    		vbus[1] ^= 1;
    	if (c == 't')
    		vbat[1] ^= 1;

    	for (int i = 0; i < 2; i++)
    	{
    		set_vbus(i, vbus[i]);
    		set_vbat(i, vbat[i]);
    	}

    	if (c > 0 && c < 128)
    	{
    		printf("q/w toggle badge 1 bus/bat; r/t toggle badge 2 bus/bat\n");
	    	printf("state: 1.bus: %d / 1.bat: %d    --   2.bus: %d / 2.bat: %d\n", vbus[0], vbat[0], vbus[1], vbat[1]);
	    	printf("fault.1: %d / fault.2 : %d\n", gpio_get(vbus_fault_pins[0]), gpio_get(vbus_fault_pins[1]));
	    	printf("\n");
	    }
    }
}

struct slot_state {
	enum state {
		WAITING_FOR_POST_LOW = 0,
		WAITING_FOR_POST_HIGH, // gone low, waiting for high
		WAITING_FOR_POST_LOW_AGAIN, // gone high, waiting for low again
		SEQUENCING_1_BOTH,
		SEQUENCING_2_VBAT_ONLY,
		SEQUENCING_3_BOTH_AGAIN,
		SEQUENCING_4_VBUS_ONLY_AGAIN,
		WAITING_FOR_POST_HIGH_AGAIN,
		OVERCURRENT_INDICATE,
		TIMEOUT_INDICATE
	} state;
	uint64_t state_start_time;
	uint32_t led_colors[2];
} slot_states[2];

#define START_STATE WAITING_FOR_POST_LOW

uint32_t now_ms()
{
	return to_ms_since_boot(get_absolute_time());
}

void advance_to_state(struct slot_state* state, enum state next)
{
	state->state = next;
	state->state_start_time = now_ms();
}

void advance_after_time(struct slot_state* state, uint time_ms, enum state next)
{
	if (now_ms() - state->state_start_time >= time_ms)
	{
		state->state = next;
		state->state_start_time = now_ms();
	}
}

#define COLOR_FAIL 0x00ff00
#define COLOR_RUN1 0xaa0022
#define COLOR_RUN2 0xff0055

void update_slot(int slotno)
{
	struct slot_state* state = &slot_states[slotno];

	if (get_vbus_faulted(slotno)) advance_to_state(state, OVERCURRENT_INDICATE);

	if (state->state != START_STATE) advance_after_time(state, 5000, TIMEOUT_INDICATE);

	state->led_colors[1] = 0x000000;

	uint32_t flash_red_color = (now_ms() % 200 > 100) ? COLOR_FAIL : 0x000000;

	switch (state->state)
	{
	case WAITING_FOR_POST_LOW: 
		printf("Slot%d: WAITING_FOR_POST_LOW\t", slotno);
		set_vbat(slotno, false); set_vbus(slotno, true);
		state->led_colors[0] = COLOR_RUN1;
		state->led_colors[1] = COLOR_RUN1;
		if (!get_post(slotno)) advance_to_state(state, WAITING_FOR_POST_HIGH);
		break;

	case WAITING_FOR_POST_HIGH: 
		printf("Slot%d: WAITING_FOR_POST_HIGH\t", slotno);
		state->led_colors[0] = COLOR_RUN2;
		if (get_post(slotno)) advance_to_state(state, WAITING_FOR_POST_LOW_AGAIN);
		break;

	case WAITING_FOR_POST_LOW_AGAIN: 
		printf("Slot%d: WAITING_FOR_POST_LOW_AGAIN\t", slotno);
		state->led_colors[0] = COLOR_RUN1;
		if (!get_post(slotno)) advance_to_state(state, SEQUENCING_1_BOTH);
		break;

	case SEQUENCING_1_BOTH: 
		printf("Slot%d: SEQUENCING_1_BOTH\t", slotno);
		state->led_colors[0] = COLOR_RUN2;
		set_vbat(slotno, true); set_vbus(slotno, true);
		advance_after_time(state, 500, SEQUENCING_2_VBAT_ONLY);
		break;

	case SEQUENCING_2_VBAT_ONLY: 
		printf("Slot%d: SEQUENCING_2_VBAT_ONLY\t", slotno);
		state->led_colors[0] = COLOR_RUN1;
		set_vbat(slotno, true); set_vbus(slotno, false);
		advance_after_time(state, 500, SEQUENCING_3_BOTH_AGAIN);
		break;

	case SEQUENCING_3_BOTH_AGAIN: 
		printf("Slot%d: SEQUENCING_3_BOTH_AGAIN\t", slotno);
		state->led_colors[0] = COLOR_RUN2;
		set_vbat(slotno, true); set_vbus(slotno, true);
		advance_after_time(state, 500, SEQUENCING_4_VBUS_ONLY_AGAIN);
		break;

	case SEQUENCING_4_VBUS_ONLY_AGAIN: 
		printf("Slot%d: SEQUENCING_4_VBUS_ONLY_AGAIN\t", slotno);
		state->led_colors[0] = COLOR_RUN1;
		set_vbat(slotno, false); set_vbus(slotno, true);
		advance_after_time(state, 500, WAITING_FOR_POST_HIGH_AGAIN);
		break;

	case WAITING_FOR_POST_HIGH_AGAIN: 
		printf("Slot%d: WAITING_FOR_POST_HIGH_AGAIN\t", slotno);
		state->led_colors[0] = COLOR_RUN2;
		if (get_post(slotno)) advance_to_state(state, START_STATE);
		break;

	case OVERCURRENT_INDICATE: 
		printf("Slot%d: OVERCURRENT_INDICATE\t", slotno);
		set_vbat(slotno, false); set_vbus(slotno, false);

		state->led_colors[0] = flash_red_color;
		state->led_colors[1] = flash_red_color;

		advance_after_time(state, 2000, START_STATE);
		break;

	case TIMEOUT_INDICATE:
		printf("Slot%d: TIMEOUT_INDICATE\n", slotno);
		set_vbat(slotno, false); set_vbus(slotno, false);

		state->led_colors[0] = flash_red_color;

		advance_after_time(state, 2000, START_STATE);
		break;
	}
}

void main()
{
	stdio_init_all();

	printf("Starting...\n");

    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
    write_pixels_same(0x555555);

    init_pin_array(vbat_enable_pins, 2, GPIO_OUT);
    init_pin_array(vbus_enable_pins, 2, GPIO_OUT);
    init_pin_array(vbus_fault_pins, 2, GPIO_IN);
    init_pin_array(post_pins, 2, GPIO_IN);

    advance_to_state(&slot_states[0], START_STATE);
    advance_to_state(&slot_states[1], START_STATE);

    while (1)
    {
    	for (int slotno = 0; slotno < 2; slotno++)
    	{
    		update_slot(slotno);
    	}

    	printf("Colors: %03x %03x %03x %03x\n", slot_states[0].led_colors[0], slot_states[0].led_colors[1],
    		slot_states[1].led_colors[0], slot_states[1].led_colors[1]);


    	write_pixels(
    		slot_states[0].led_colors[0], slot_states[0].led_colors[1],
    		slot_states[1].led_colors[0], slot_states[1].led_colors[1]
    	);

    	sleep_ms(25);

    	// write_pixels_same(0xff0000);
    }
}