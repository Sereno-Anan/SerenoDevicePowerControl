/* 独立電源充電制御用 Eneloop 3本 */
#include <Arduino.h>
#define VernNo "Ver1.00"

// WDTタイムアウト
#include <avr/wdt.h>
volatile byte wdt_cycle = 0; // WDTタイムアウトカウント
#define timeout_count 35     // 5分 (35) WDTソフトリセット時間設定 RTC無し前提 (+ 誤差が増えるのを抑制)

// SleepMode
#include <avr/sleep.h>
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#define TRON 10 // PV の 8.2Ω 短絡による電流測定ON
#define USBON 6 // USB の 電源ON

/* 変数定義 */
int Time_n = 0;                    // 時刻カウンタ 初期値0
byte wdt;                          // 受信した時刻のwdt
float Eneloop = 4.88;              // Eneloop電池電圧
#define Eneloop_const 0.0064453125 // Eneloop電池電圧AD値からの換算係数
int Radiation = 0;                 // 2Wパネル測定日射量
#define Radiation_constant 1.34    // 2Wパネル日射量センサ定数 Fan有り1.41 Fan無し1.34

/* 関数定義 */
void software_Reset();
void system_sleep();
float ReadSens_ch(int ch, int n, int intarvalms);
void WDT_setup8();
ISR(WDT_vect);
void software_Reset();

void setup()
{
  Serial.begin(9600);
  pinMode(0, INPUT); // D0ポートをハイインピーダンスに設定 (GND接続するため)
  pinMode(TRON, OUTPUT);
  digitalWrite(TRON, LOW); // Tr OFF
  pinMode(USBON, OUTPUT);
  digitalWrite(USBON, HIGH); // USB電源 ON
  WDT_setup8();              // 8秒のWDT設定
}

void loop()
{
  Time_n++;
  digitalWrite(USBON, HIGH); // USB電源 ON
  delay(50);
  Eneloop = Eneloop_const * ReadSens_ch(1, 3, 50); // Eneloop電圧AD1の4回平均値(個別ch, 読取回数, intarvalms)
  digitalWrite(TRON, HIGH);                        // TR ON  2Wパネル日射量測定の為太陽電池出力短絡ON
  delay(50);
  Radiation = 0.9 + Radiation_constant * ReadSens_ch(3, 3, 50); // 2Wパネル日射量AD3平均値 +0.9でint切り捨て0→1のしきい値を下げる
  digitalWrite(TRON, LOW);                                      // TR OFF 太陽電池出力短絡OFF

  // digitalWrite(USBON,LOW); // USB電源 OFF

  // Eneloop電圧AD1の4回平均値 (個別ch, 読取回数, intarvalms)(delay200も兼ねている)
  Eneloop = Eneloop_const * ReadSens_ch(1, 4, 50);

  // Eneloop電池電圧が4.3V以上のとき、電流測定回路をONにして過充電を防止する。
  if (Eneloop > 4.3)
  {
    digitalWrite(TRON, HIGH); // TR ON  電流測定回路ON
  }

  // Sleep mode Setup
  do
  {
    system_sleep();
  } while (wdt_cycle < timeout_count); // timeout_count(x8秒)以上経過したら抜ける
  wdt_cycle = 0;                       // WDTのカウントのリセット
}

// システム停止
void system_sleep()
{
  cbi(ADCSRA, ADEN);                   // ADC 回路電源をOFF (ADC使って無くても120μA消費するため)
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // パワーダウンモード
  sleep_enable();
  sleep_mode();      // ここでスリープに入る
  sleep_disable();   // RTCからの割り込みが発生したらここから動作再開
  sbi(ADCSRA, ADEN); // ADC ON
}

float ReadSens_ch(int ch, int n, int intarvalms)
{
  int sva = 0;

  // n回平均
  for (int i = 0; i < n; i++)
  {
    delay(intarvalms);
    sva = (analogRead(ch) + sva);
  }
  return sva / n;
}

// ウォッチドッグタイマーをセット。
void WDT_setup8()
{
  // WDTCSR にセットする WDP0-WDP3 の値。9=8sec
  byte bb = 9;
  bb = bb & 7;    // 下位3ビットをbbに
  bb |= (1 << 5); // bbの5ビット目 (WDP3) を 1 にする
  bb |= (1 << WDCE);
  MCUSR &= ~(1 << WDRF); // MCU Status Reg. Watchdog Reset Flag -> 0
  // start timed sequence
  // ウォッチドッグ変更許可 (WDCEは4サイクルで自動リセット)
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  // set new watchdog timeout value
  // 制御レジスタを設定
  WDTCSR = bb;
  WDTCSR |= _BV(WDIE);
}

// WDTがタイムアップした時に実行される処理
ISR(WDT_vect)
{
  wdt_cycle++;

  // 8秒 x 250 (33分) 以上経過したらソフトリセット
  if (wdt_cycle >= 250)
  {
    software_Reset();
  }
}

void software_Reset()
{
  asm volatile("  jmp 0");
}
