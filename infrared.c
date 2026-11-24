#include "infrared.h"
#include "ch32fun.h"
#include <string.h>
#include "ir_codec.h"

#define IR_TX_PIN 21  // PB21: IR LED cathode (anode to VCC via R32), low = LED on
#define IR_RX_PIN 19  // PB19: IR receiver output (10k pull-up R27), low = carrier present
#define IR_RX_LINE (1u << IR_RX_PIN)

// Carrier: TIM2 CH1 PWM on PB21 (TIM2_RM = 001, CH1/PB21; CH643RM 8.3.2.1)
#define IR_CARRIER_HZ 38000
#define IR_CARRIER_PERIOD (FUNCONF_SYSTEM_CORE_CLOCK / IR_CARRIER_HZ)  // TIM2 ticks
#define IR_CARRIER_DUTY (IR_CARRIER_PERIOD / 3)
#define US_TO_CARRIER_PERIODS(us) (((us) * IR_CARRIER_HZ + 500000) / 1000000)

#define NEC_REPEAT_TIMEOUT_MS 250  // repeats arrive every 108ms while a button is held

static bool s_initialized;

// ---- Transmit ----

// The frame going out (ir_codec.h), and where in it: the segment (mark or
// space) and the carrier periods left in it (including this one) of the
// last period written to the CH1CVR preload register
static ir_frame_t s_tx_frame;
static uint16_t s_tx_count;
static uint16_t s_tx_index;
static uint16_t s_tx_remaining;
static volatile bool s_tx_busy;

static ir_rx_t s_rx;  // forward: reset when our own transmission ends

static void pb_set_mode(unsigned pin, GPIO_CFGLR_PIN_MODE_Typedef mode) {
    __IO uint32_t* cfg = pin < 8 ? &GPIOB->CFGLR : pin < 16 ? &GPIOB->CFGHR : &GPIOB->CFGXR;
    unsigned shift = 4 * (pin & 7);
    *cfg = (*cfg & ~(0xFu << shift)) | ((uint32_t)mode << shift);
}

static uint16_t segment_periods(uint16_t i) { return (uint16_t)US_TO_CARRIER_PERIODS((uint32_t)IrFrame_SegmentUs(&s_tx_frame, i)); }

// CH1CVR is preloaded, so each value takes effect for exactly one whole carrier
// period, starting at the next update event
static void tx_schedule_next_period(void) {
    if (--s_tx_remaining == 0) {
        if (++s_tx_index == s_tx_count) {
            TIM2->CH1CVR = 0;  // done: carrier off after the final mark
            return;
        }
        s_tx_remaining = segment_periods(s_tx_index);
    }
    TIM2->CH1CVR = (s_tx_index & 1) ? 0 : IR_CARRIER_DUTY;
}

// Sends s_tx_frame
static void tx_start(void) {
    s_tx_busy = true;
    s_tx_count = IrFrame_Segments(&s_tx_frame);
    s_tx_index = 0;
    s_tx_remaining = segment_periods(0);

    TIM2->CH1CVR = IR_CARRIER_DUTY;  // first period of the leader mark
    TIM2->SWEVGR = TIM_UG;           // restart the period and latch it now
    TIM2->INTFR = (uint16_t)~TIM_UIF;
    tx_schedule_next_period();
    TIM2->DMAINTENR |= TIM_UIE;
    TIM2->CTLR1 |= TIM_CEN;
}

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void) {
    TIM2->INTFR = (uint16_t)~TIM_UIF;
    if (s_tx_index >= s_tx_count) {
        // The carrier-off period has started: stop, output stays inactive (LED off)
        TIM2->CTLR1 &= ~TIM_CEN;
        TIM2->DMAINTENR &= ~TIM_UIE;
        IrRx_Reset(&s_rx);  // discard whatever the receiver picked up from our own LED
        s_tx_busy = false;
        return;
    }
    tx_schedule_next_period();
}

bool infrared_send_nec_raw(uint32_t data) {
    if (!s_initialized || s_tx_busy) return false;
    IrFrame_Nec(&s_tx_frame, data);
    tx_start();
    return true;
}

bool infrared_send_nec(uint16_t address, uint8_t command) {
    uint32_t data = address > 0xFF ? address : address | ((uint32_t)(uint8_t)~address << 8);
    data |= ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);
    return infrared_send_nec_raw(data);
}

bool infrared_send_nec_repeat(void) {
    if (!s_initialized || s_tx_busy) return false;
    IrFrame_NecRepeat(&s_tx_frame);
    tx_start();
    return true;
}

bool infrared_send_packet(uint8_t type, const uint8_t* data, uint8_t length) {
    if (!s_initialized || s_tx_busy || !IrFrame_Packet(&s_tx_frame, type, data, length)) return false;
    tx_start();
    return true;
}

bool infrared_send_shot(uint16_t bits) {
    if (!s_initialized || s_tx_busy) return false;
    IrFrame_Shot(&s_tx_frame, bits);
    tx_start();
    return true;
}

bool infrared_tx_busy(void) {
    return s_tx_busy;
}

// ---- Receive ----

#define RX_QUEUE_SIZE 8  // power of two
static infrared_nec_message_t s_rx_queue[RX_QUEUE_SIZE];
static volatile uint8_t s_rx_head, s_rx_tail;

#define RX_PACKETS 4  // power of two; holds RX_PACKETS - 1
static infrared_packet_t s_rx_packets[RX_PACKETS];
static volatile uint8_t s_rx_packet_head, s_rx_packet_tail;

static uint32_t s_rx_last_edge;
static infrared_nec_message_t s_rx_last_message;
static uint32_t s_rx_last_message_time;
static bool s_rx_last_message_valid;

static void rx_push(const infrared_nec_message_t* message, uint32_t now) {
    s_rx_last_message = *message;
    s_rx_last_message_time = now;
    s_rx_last_message_valid = true;

    uint8_t next = (s_rx_head + 1) & (RX_QUEUE_SIZE - 1);
    if (next == s_rx_tail) return;  // full, drop
    s_rx_queue[s_rx_head] = *message;
    s_rx_head = next;
}

static void rx_frame_done(uint32_t now) {
    uint8_t address = s_rx.bytes[0], address_inv = s_rx.bytes[1];
    uint8_t command = s_rx.bytes[2], command_inv = s_rx.bytes[3];
    if ((uint8_t)(command ^ command_inv) != 0xFF) return;

    infrared_nec_message_t message = {
        .address = (uint8_t)(address ^ address_inv) == 0xFF ? address : (uint16_t)(address | address_inv << 8),
        .command = command,
        .repeat = false,
    };
    rx_push(&message, now);
}

static void rx_repeat_done(uint32_t now) {
    if (!s_rx_last_message_valid || now - s_rx_last_message_time > NEC_REPEAT_TIMEOUT_MS * DELAY_MS_TIME) return;
    infrared_nec_message_t message = s_rx_last_message;
    message.repeat = true;
    rx_push(&message, now);
}

static void rx_packet_done(void) {
    uint8_t next = (s_rx_packet_head + 1) & (RX_PACKETS - 1);
    if (next == s_rx_packet_tail) return;  // full, drop
    infrared_packet_t* p = &s_rx_packets[s_rx_packet_head];
    p->type = s_rx.bytes[0];
    p->length = s_rx.bytes[1];
    memcpy(p->data, &s_rx.bytes[2], p->length);
    s_rx_packet_head = next;
}

// Shots (16-bit frames, ir_codec.h) as they came in, CRC not yet checked
#define RX_SHOTS 8  // power of two; holds RX_SHOTS - 1
static uint16_t s_rx_shots[RX_SHOTS];
static volatile uint8_t s_rx_shot_head, s_rx_shot_tail;

static void rx_shot_done(void) {
    uint8_t next = (s_rx_shot_head + 1) & (RX_SHOTS - 1);
    if (next == s_rx_shot_tail) return;  // full, drop
    s_rx_shots[s_rx_shot_head] = (uint16_t)(s_rx.bytes[0] | s_rx.bytes[1] << 8);
    s_rx_shot_head = next;
}

// Every mark and space as it came in, for looking at what other devices
// send (tools/badge.py irlog): bit 15 set for a mark, the rest the length
// in us (at most 32767)
#define RAW_LOG 128  // power of two
static uint16_t s_raw[RAW_LOG];
static volatile uint8_t s_raw_head, s_raw_tail;

unsigned infrared_raw_read(uint16_t* out, unsigned max) {
    unsigned n = 0;
    while (n < max && s_raw_tail != s_raw_head) {
        out[n++] = s_raw[s_raw_tail];
        s_raw_tail = (uint8_t)((s_raw_tail + 1) & (RAW_LOG - 1));
    }
    return n;
}

// Only EXTI line 19 is used as an interrupt in lines 16-25 (the buttons use
// lines 18/20 as wake-up events only, see standby.c)
void EXTI25_16_IRQHandler(void) __attribute__((interrupt));
void EXTI25_16_IRQHandler(void) {
    uint32_t now = SysTick->CNTL;
    if (!(EXTI->INTFR & IR_RX_LINE)) return;
    EXTI->INTFR = IR_RX_LINE;

    // low = carrier: a mark starts now, so the interval that ended was a space
    bool ended_mark = GPIOB->INDR & IR_RX_LINE;
    uint32_t us = (now - s_rx_last_edge) / DELAY_US_TIME;
    s_rx_last_edge = now;

    // the raw log takes our own transmission too, if the receiver sees it
    uint8_t next = (uint8_t)((s_raw_head + 1) & (RAW_LOG - 1));
    if (next != s_raw_tail) {  // full: the newest are dropped
        s_raw[s_raw_head] = (uint16_t)((ended_mark ? 0x8000u : 0) | (us < 0x7FFF ? us : 0x7FFF));
        s_raw_head = next;
    }
    if (s_tx_busy) {
        IrRx_Reset(&s_rx);  // ignore our own transmission
        return;
    }
    switch (IrRx_Interval(&s_rx, ended_mark, us)) {
        case IR_RX_NEC: rx_frame_done(now); break;
        case IR_RX_NEC_REPEAT: rx_repeat_done(now); break;
        case IR_RX_PACKET: rx_packet_done(); break;
        case IR_RX_SHOT: rx_shot_done(); break;
        default: break;
    }
}

bool infrared_receive(infrared_nec_message_t* message) {
    if (s_rx_tail == s_rx_head) return false;
    *message = s_rx_queue[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) & (RX_QUEUE_SIZE - 1);
    return true;
}

bool infrared_receive_packet(infrared_packet_t* packet) {
    if (s_rx_packet_tail == s_rx_packet_head) return false;
    *packet = s_rx_packets[s_rx_packet_tail];
    s_rx_packet_tail = (s_rx_packet_tail + 1) & (RX_PACKETS - 1);
    return true;
}

bool infrared_receive_shot(ir_shot_t* shot) {
    if (!s_initialized) return false;
    // A shot ends in silence, with no edge after it: look whether the line
    // has been quiet long enough (with the edge interrupt held off)
    NVIC_DisableIRQ(EXTI25_16_IRQn);
    uint32_t quiet_us = (SysTick->CNTL - s_rx_last_edge) / DELAY_US_TIME;
    if (IrRx_Silence(&s_rx, quiet_us) == IR_RX_SHOT) rx_shot_done();
    NVIC_EnableIRQ(EXTI25_16_IRQn);
    while (s_rx_shot_tail != s_rx_shot_head) {
        uint16_t bits = s_rx_shots[s_rx_shot_tail];
        s_rx_shot_tail = (s_rx_shot_tail + 1) & (RX_SHOTS - 1);
        if (IrShot_Decode(bits, shot)) return true;  // a bad CRC: the next one
    }
    return false;
}

// ---- Init ----

void infrared_init(void) {
    RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPBEN;
    RCC->APB1PCENR |= RCC_TIM2EN;

    // TX: TIM2 CH1, PWM mode 1 with preload, active low output (pin low = LED
    // on for the first third of each carrier period). TIM2 is an advanced
    // timer here, so the outputs also need MOE.
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_TIM2_REMAP) | AFIO_PCFR1_TIM2_REMAP_0;
    TIM2->CTLR1 = TIM_ARPE;
    TIM2->PSC = 0;
    TIM2->ATRLR = IR_CARRIER_PERIOD - 1;
    TIM2->CH1CVR = 0;
    TIM2->CHCTLR1 = (TIM2->CHCTLR1 & ~0xFFu) | TIM_OC1M_2 | TIM_OC1M_1 | TIM_OC1PE;
    TIM2->CCER = TIM_CC1E | TIM_CC1P;
    TIM2->BDTR = TIM_MOE;
    TIM2->SWEVGR = TIM_UG;
    TIM2->INTFR = (uint16_t)~TIM_UIF;
    s_tx_busy = false;
    GPIOB->BSXR = 1u << (IR_TX_PIN - 16);  // high (LED off) in case the AF isn't driving yet
    pb_set_mode(IR_TX_PIN, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    NVIC_EnableIRQ(TIM2_IRQn);

    // RX: both edges of the receiver output on EXTI line 19
    pb_set_mode(IR_RX_PIN, GPIO_CFGLR_IN_FLOAT);  // external pull-up
    AFIO->EXTICR2 = (AFIO->EXTICR2 & ~AFIO_EXTICR2_EXTI19) | AFIO_EXTICR2_EXTI19_PB;
    IrRx_Reset(&s_rx);
    s_rx_last_edge = SysTick->CNTL;
    EXTI->RTENR |= IR_RX_LINE;
    EXTI->FTENR |= IR_RX_LINE;
    EXTI->INTFR = IR_RX_LINE;
    EXTI->INTENR |= IR_RX_LINE;
    NVIC_EnableIRQ(EXTI25_16_IRQn);

    s_initialized = true;
}

bool infrared_deinit(void) {
    bool was_initialized = s_initialized;
    s_initialized = false;

    NVIC_DisableIRQ(EXTI25_16_IRQn);
    EXTI->INTENR &= ~IR_RX_LINE;
    EXTI->RTENR &= ~IR_RX_LINE;
    EXTI->FTENR &= ~IR_RX_LINE;
    EXTI->INTFR = IR_RX_LINE;

    NVIC_DisableIRQ(TIM2_IRQn);
    if (RCC->APB1PCENR & RCC_TIM2EN) {
        TIM2->CTLR1 &= ~TIM_CEN;
        TIM2->DMAINTENR &= ~TIM_UIE;
    }
    s_tx_busy = false;

    // Drive the LED cathode high: LED off with 0V across it, pin not floating
    GPIOB->BSXR = 1u << (IR_TX_PIN - 16);
    pb_set_mode(IR_TX_PIN, GPIO_CFGLR_OUT_2Mhz_PP);
    RCC->APB1PCENR &= ~RCC_TIM2EN;

    return was_initialized;
}
