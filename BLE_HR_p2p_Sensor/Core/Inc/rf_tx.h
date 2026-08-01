#ifndef RF_TX_H
#define RF_TX_H

/* 30 MHz transmitter control
 * PB5 = TX enable (1=on, 0=off)
 * PA6 = channel bit0, PB8 = channel bit1  → ch: 0..3
 * PA8 = power bit0,   PA1 = power bit1    → level: 1..4
 *
 * Cycle: CH sweep (pwr=1, ch=0→3, 100ms each)
 *      → PWR sweep (ch=0, pwr=1→4, 100ms each)
 *      → 1 s pause → repeat
 */
void RF_TX_Init(void);
void RF_TX_Start(void);

#endif /* RF_TX_H */
