#include "infrared.h"
#include "ch32fun.h"

#define IR_TX_PIN 21  // PB21: IR LED cathode (anode to VCC via R32), low = LED on
#define IR_RX_PIN 19  // PB19: IR receiver output (10k pull-up R27), low = carrier present
#define IR_RX_LINE (1u << IR_RX_PIN)

// Carrier: TIM2 CH1 PWM on PB21 (TIM2_RM = 001, CH1/PB21; CH643RM 8.3.2.1)
#define IR_CARRIER_HZ 38000
#define IR_CARRIER_PERIOD (FUNCONF_SYSTEM_CORE_CLOCK / IR_CARRIER_HZ)  // TIM2 ticks
#define IR_CARRIER_DUTY (IR_CARRIER_PERIOD / 3)
#define US_TO_CARRIER_PERIODS(us) (((us) * IR_CARRIER_HZ + 500000) / 1000000)

// NEC timings in microseconds
#define NEC_LEADER_MARK 9000
#define NEC_LEADER_SPACE 4500
#define NEC_REPEAT_SPACE 2250
#define NEC_BIT_MARK 562
#define NEC_ZERO_SPACE 562
#define NEC_ONE_SPACE 1687
#define NEC_REPEAT_TIMEOUT_MS 250  // repeats arrive every 108ms while a button is held

static bool s_initialized;

// ---- Transmit ----

// Alternating mark/space durations in carrier periods, starting with a mark
static uint16_t s_tx_segments[2 + 2 * 32 + 1];
static uint8_t s_tx_count;
// Segment and the periods left in it (including itself) of the last period
// written to the CH1CVR preload register
static uint8_t s_tx_index;
static uint16_t s_tx_remaining;
static volatile bool s_tx_busy;

static volatile uint8_t s_rx_state;  // forward: reset when our own transmission ends

static void pb_set_mode(unsigned pin, GPIO_CFGLR_PIN_MODE_Typedef mode) {
    __IO uint32_t* cfg = pin < 8 ? &GPIOB->CFGLR : pin < 16 ? &GPIOB->CFGHR : &GPIOB->CFGXR;
    unsigned shift = 4 * (pin & 7);
    *cfg = (*cfg & ~(0xFu << shift)) | ((uint32_t)mode << shift);
}

// CH1CVR is preloaded, so each value takes effect for exactly one whole carrier
// period, starting at the next update event
static void tx_schedule_next_period(void) {
    if (--s_tx_remaining == 0) {
        if (++s_tx_index == s_tx_count) {
            TIM2->CH1CVR = 0;  // done: carrier off after the final mark
            return;
        }
        s_tx_remaining = s_tx_segments[s_tx_index];
    }
    TIM2->CH1CVR = (s_tx_index & 1) ? 0 : IR_CARRIER_DUTY;
}

static void tx_start(void) {
    s_tx_busy = true;
    s_tx_index = 0;
    s_tx_remaining = s_tx_segments[0];

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
        s_rx_state = 0;  // discard whatever the receiver picked up from our own LED
        s_tx_busy = false;
        return;
    }
    tx_schedule_next_period();
}

bool infrared_send_nec_raw(uint32_t data) {
    if (!s_initialized || s_tx_busy) return false;
    uint8_t n = 0;
    s_tx_segments[n++] = US_TO_CARRIER_PERIODS(NEC_LEADER_MARK);
    s_tx_segments[n++] = US_TO_CARRIER_PERIODS(NEC_LEADER_SPACE);
    for (unsigned bit = 0; bit < 32; bit++) {
        s_tx_segments[n++] = US_TO_CARRIER_PERIODS(NEC_BIT_MARK);
        s_tx_segments[n++] =
            ((data >> bit) & 1) ? US_TO_CARRIER_PERIODS(NEC_ONE_SPACE) : US_TO_CARRIER_PERIODS(NEC_ZERO_SPACE);
    }
    s_tx_segments[n++] = US_TO_CARRIER_PERIODS(NEC_BIT_MARK);  // stop mark
    s_tx_count = n;
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
    s_tx_segments[0] = US_TO_CARRIER_PERIODS(NEC_LEADER_MARK);
    s_tx_segments[1] = US_TO_CARRIER_PERIODS(NEC_REPEAT_SPACE);
    s_tx_segments[2] = US_TO_CARRIER_PERIODS(NEC_BIT_MARK);
    s_tx_count = 3;
    tx_start();
    return true;
}

bool infrared_tx_busy(void) {
    return s_tx_busy;
}

// ---- Receive ----

enum {
    RX_IDLE = 0,       // waiting for a mark to start
    RX_LEADER_MARK,    // in a mark that may be a 9ms leader
    RX_LEADER_SPACE,   // after the leader: 4.5ms (frame) or 2.25ms (repeat)
    RX_BIT_MARK,       // in a 562us bit mark
    RX_BIT_SPACE,      // after a bit mark: 562us (0) or 1687us (1)
    RX_REPEAT_MARK,    // in the final mark of a repeat code
    RX_STOP_MARK,      // in the final mark of a frame
};

#define RX_QUEUE_SIZE 8  // power of two
static infrared_nec_message_t s_rx_queue[RX_QUEUE_SIZE];
static volatile uint8_t s_rx_head, s_rx_tail;

static uint32_t s_rx_last_edge;
static uint32_t s_rx_bits;
static uint8_t s_rx_bit_count;
static infrared_nec_message_t s_rx_last_message;
static uint32_t s_rx_last_message_time;
static bool s_rx_last_message_valid;

static bool in_range(uint32_t us, uint32_t nominal) {
    return us > nominal * 6 / 10 && us < nominal * 14 / 10;
}

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
    uint8_t address = s_rx_bits, address_inv = s_rx_bits >> 8;
    uint8_t command = s_rx_bits >> 16, command_inv = s_rx_bits >> 24;
    if ((uint8_t)(command ^ command_inv) != 0xFF) return;

    infrared_nec_message_t message = {
        .address = (uint8_t)(address ^ address_inv) == 0xFF ? address : (uint16_t)(s_rx_bits & 0xFFFF),
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

// mark_started: a falling edge (the interval that just ended was a space),
// otherwise a rising edge (the interval that just ended was a mark)
static void rx_edge(bool mark_started, uint32_t us, uint32_t now) {
    if (mark_started) {
        switch (s_rx_state) {
            case RX_LEADER_SPACE:
                if (in_range(us, NEC_LEADER_SPACE)) {
                    s_rx_bits = 0;
                    s_rx_bit_count = 0;
                    s_rx_state = RX_BIT_MARK;
                    return;
                }
                if (in_range(us, NEC_REPEAT_SPACE)) {
                    s_rx_state = RX_REPEAT_MARK;
                    return;
                }
                break;
            case RX_BIT_SPACE:
                if (in_range(us, NEC_ZERO_SPACE) || in_range(us, NEC_ONE_SPACE)) {
                    if (us > (NEC_ZERO_SPACE + NEC_ONE_SPACE) / 2) s_rx_bits |= 1u << s_rx_bit_count;
                    if (++s_rx_bit_count == 32) {
                        rx_frame_done(now);
                        s_rx_state = RX_STOP_MARK;
                    } else {
                        s_rx_state = RX_BIT_MARK;
                    }
                    return;
                }
                break;
        }
        // Anything unexpected: this mark could be the start of a new leader
        s_rx_state = RX_LEADER_MARK;
    } else {
        switch (s_rx_state) {
            case RX_LEADER_MARK:
                s_rx_state = in_range(us, NEC_LEADER_MARK) ? RX_LEADER_SPACE : RX_IDLE;
                return;
            case RX_BIT_MARK:
                s_rx_state = in_range(us, NEC_BIT_MARK) ? RX_BIT_SPACE : RX_IDLE;
                return;
            case RX_REPEAT_MARK:
                if (in_range(us, NEC_BIT_MARK)) rx_repeat_done(now);
                break;
        }
        s_rx_state = RX_IDLE;
    }
}

// Only EXTI line 19 is used as an interrupt in lines 16-25 (the buttons use
// lines 18/20 as wake-up events only, see standby.c)
void EXTI25_16_IRQHandler(void) __attribute__((interrupt));
void EXTI25_16_IRQHandler(void) {
    uint32_t now = SysTick->CNTL;
    if (!(EXTI->INTFR & IR_RX_LINE)) return;
    EXTI->INTFR = IR_RX_LINE;

    bool mark = !(GPIOB->INDR & IR_RX_LINE);
    uint32_t us = (now - s_rx_last_edge) / DELAY_US_TIME;
    s_rx_last_edge = now;

    if (s_tx_busy) {
        s_rx_state = RX_IDLE;  // ignore our own transmission
        return;
    }
    rx_edge(mark, us, now);
}

bool infrared_receive(infrared_nec_message_t* message) {
    if (s_rx_tail == s_rx_head) return false;
    *message = s_rx_queue[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) & (RX_QUEUE_SIZE - 1);
    return true;
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
    s_rx_state = RX_IDLE;
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
