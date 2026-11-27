#include "adc_console.h"
#include "adc_config.h"
#include "ads1256.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static ADS1256_HandleTypeDef adc = {
  .reference_uv = ADC_REFERENCE_UV,
  .rate = ADC_START_RATE,
  .channel = ADC_START_CHANNEL,
  .gain = ADC_START_GAIN,
  .error = "not initialized"
};
static UART_HandleTypeDef *adc_uart;
static uint32_t last_report;
static uint8_t streaming;

static void ADC_Print(const char *format, ...)
{
  char output[256];
  va_list arguments;
  va_start(arguments, format);
  int length = vsnprintf(output, sizeof(output), format, arguments);
  va_end(arguments);
  if (length > 0)
  {
    uint16_t count = (uint16_t)((size_t)length < sizeof(output) ?
                               (size_t)length : sizeof(output) - 1U);
    HAL_UART_Transmit(adc_uart, (uint8_t *)output, count, 200U);
  }
}

static uint8_t ADC_ParseUnsigned(const char *text, uint32_t *value)
{
  if (*text == '\0')
  {
    return 0U;
  }
  uint32_t parsed = 0U;
  while (*text != '\0')
  {
    if (!isdigit((unsigned char)*text))
    {
      return 0U;
    }
    uint32_t digit = (uint32_t)(*text++ - '0');
    if (parsed > (UINT32_MAX - digit) / 10U)
    {
      return 0U;
    }
    parsed = parsed * 10U + digit;
  }
  *value = parsed;
  return 1U;
}

static uint8_t ADC_Stale(void)
{
  return (uint32_t)(HAL_GetTick() - adc.sample_tick) > 1000U;
}

static uint8_t ADC_CalibrationActive(void)
{
  return ADC_CALIBRATION_ENABLED &&
         adc.channel == ADC_CALIBRATION_CHANNEL &&
         adc.gain == ADC_CALIBRATION_GAIN &&
         adc.rate == ADC_CALIBRATION_RATE &&
         adc.reference_uv == ADC_CALIBRATION_REFERENCE_UV;
}

static int32_t ADC_CalibratedVoltage(int32_t voltage_uv)
{
  if (!ADC_CalibrationActive())
  {
    return voltage_uv;
  }
  int64_t scaled = ((int64_t)voltage_uv - ADC_CALIBRATION_OFFSET_UV) *
                   ADC_CALIBRATION_GAIN_PPM;
  return (int32_t)((scaled + (scaled >= 0 ? 500000LL : -500000LL)) / 1000000LL);
}

static void ADC_Status(void)
{
  ADC_Print("ADC %s ID=%u AIN%u-AINCOM GAIN=%u RATE=%luSPS VREF=%lumV BUF=OFF STREAM=%s CAL=%s\r\n",
            adc.initialized ? (ADC_Stale() ? "STALE" : "OK") : "NOT_READY",
            (unsigned)adc.chip_id, (unsigned)adc.channel, (unsigned)adc.gain,
            (unsigned long)adc.rate, (unsigned long)(adc.reference_uv / 1000U),
            streaming ? "ON" : "OFF", ADC_CalibrationActive() ? "ON" : "OFF");
  if (ADC_CalibrationActive())
  {
    ADC_Print("ADC CAL OFFSET=%lduV SCALE=%ld/1000000\r\n",
              (long)ADC_CALIBRATION_OFFSET_UV, (long)ADC_CALIBRATION_GAIN_PPM);
  }
  ADC_Print("ADC N=%lu DRDY_EDGES=%lu HAS_SAMPLE=%u AGE=%lums ERROR=%s\r\n",
            (unsigned long)adc.samples, (unsigned long)adc.drdy_edges,
            (unsigned)adc.has_sample,
            (unsigned long)(HAL_GetTick() - adc.sample_tick), adc.error);
}

static void ADC_Report(void)
{
  if (!adc.initialized)
  {
    ADC_Print("ERR ADC %s; send ADC INIT\r\n", adc.error);
    return;
  }
  if (!adc.has_sample || ADC_Stale())
  {
    ADC_Print("ERR ADC no fresh sample; check DRDY/power, then ADC INIT\r\n");
    return;
  }
  int32_t voltage_uv = ADC_CalibratedVoltage(adc.voltage_uv);
  uint32_t magnitude = (uint32_t)(voltage_uv < 0 ? -voltage_uv : voltage_uv);
  uint32_t uncalibrated_magnitude = (uint32_t)(adc.voltage_uv < 0 ?
                                              -adc.voltage_uv : adc.voltage_uv);
  ADC_Print("ADC AIN%u RAW=%ld U=%s%lu.%03lumV U_UNCAL=%s%lu.%03lumV AGE=%lums N=%lu SAT=%u\r\n",
            (unsigned)adc.channel, (long)adc.raw,
            voltage_uv < 0 ? "-" : "",
            (unsigned long)(magnitude / 1000U),
            (unsigned long)(magnitude % 1000U),
            adc.voltage_uv < 0 ? "-" : "",
            (unsigned long)(uncalibrated_magnitude / 1000U),
            (unsigned long)(uncalibrated_magnitude % 1000U),
            (unsigned long)(HAL_GetTick() - adc.sample_tick),
            (unsigned long)adc.samples,
            (unsigned)(adc.raw == -8388608 || adc.raw == 8388607));
}

void ADC_Console_Help(void)
{
  ADC_Print("ADC INIT | STATUS | READ | ON | OFF | HELP\r\n");
  ADC_Print("ADC CH <0..7> | GAIN <1|2|4|8|16|32|64> | RATE <SPS> | VREF <mV>\r\n");
  ADC_Print("ADC rates: 10,15,25,30,50,60,100,500,1000,2000,3750,7500,15000,30000\r\n");
  ADC_Print("ADC ON reports latest voltage every %lums; not a lossless sample stream.\r\n",
            (unsigned long)ADC_REPORT_INTERVAL_MS);
  ADC_Print("ADC U=calibrated when CAL=ON; U_UNCAL=before calibration; RAW=original code.\r\n");
}

uint8_t ADC_Console_Command(char *const *tokens, uint32_t count)
{
  if (count == 0U || strcmp(tokens[0], "adc") != 0)
  {
    return 0U;
  }
  if (count == 1U || (count == 2U && strcmp(tokens[1], "help") == 0))
  {
    ADC_Console_Help();
    return 1U;
  }
  if (count == 2U)
  {
    if (strcmp(tokens[1], "status") == 0)
    {
      ADC_Status();
      return 1U;
    }
    if (strcmp(tokens[1], "init") == 0)
    {
      streaming = 0U;
      ADC_Print("ADC initializing...\r\n");
      ADS1256_Init(&adc);
      ADC_Status();
      return 1U;
    }
    if (strcmp(tokens[1], "off") == 0)
    {
      streaming = 0U;
      ADC_Print("OK ADC STREAM OFF; conversions continue\r\n");
      return 1U;
    }
    if (strcmp(tokens[1], "read") == 0 || strcmp(tokens[1], "on") == 0)
    {
      ADS1256_Poll(&adc);
      if (strcmp(tokens[1], "on") == 0 && adc.initialized)
      {
        streaming = 1U;
        last_report = HAL_GetTick();
        ADC_Print("OK ADC STREAM ON interval=%lums\r\n",
                  (unsigned long)ADC_REPORT_INTERVAL_MS);
      }
      ADC_Report();
      return 1U;
    }
  }
  uint32_t value;
  if (count == 3U && ADC_ParseUnsigned(tokens[2], &value))
  {
    if (strcmp(tokens[1], "vref") == 0 && value >= 500U && value <= 3300U)
    {
      ADS1256_SetReference(&adc, value * 1000U);
      ADC_Print("OK ADC VREF=%lumV; software scaling only, use measured VREF; CAL=%s\r\n",
                (unsigned long)value, ADC_CalibrationActive() ? "ON" : "OFF");
      return 1U;
    }
    uint8_t channel = adc.channel;
    uint8_t gain = adc.gain;
    uint32_t rate = adc.rate;
    if (strcmp(tokens[1], "ch") == 0 && value <= 7U)
    {
      channel = (uint8_t)value;
    }
    else if (strcmp(tokens[1], "gain") == 0 && value <= 64U)
    {
      gain = (uint8_t)value;
    }
    else if (strcmp(tokens[1], "rate") == 0)
    {
      rate = value;
    }
    else
    {
      goto syntax_error;
    }
    if (!ADS1256_ValidSettings(channel, gain, rate))
    {
      goto syntax_error;
    }
    if (!adc.initialized)
    {
      ADC_Print("ERR ADC not ready; send ADC INIT\r\n");
      return 1U;
    }
    streaming = 0U;
    ADS1256_Configure(&adc, channel, gain, rate);
    ADC_Status();
    return 1U;
  }

syntax_error:
  ADC_Print("ERR ADC syntax/value; send ADC HELP\r\n");
  return 1U;
}

void ADC_Console_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart)
{
  adc.spi = spi;
  adc_uart = uart;
  ADC_Print("\r\nADS1256 | SPI3 Mode1 1.25MHz | DRDY=PB0 | VREF assumption=%lumV\r\n",
            (unsigned long)(adc.reference_uv / 1000U));
  ADS1256_Init(&adc);
  ADC_Status();
  ADC_Print("Low-pass detector voltage: AIN%u to AGND. Send ADC ON or ADC HELP.\r\n",
            (unsigned)adc.channel);
}

void ADC_Console_Process(void)
{
  if (adc.initialized)
  {
    HAL_StatusTypeDef result = ADS1256_Poll(&adc);
    if (result != HAL_OK && result != HAL_BUSY)
    {
      streaming = 0U;
      ADC_Print("ERR ADC %s\r\n", adc.error);
    }
  }
  if (streaming && (uint32_t)(HAL_GetTick() - last_report) >= ADC_REPORT_INTERVAL_MS)
  {
    last_report = HAL_GetTick();
    ADC_Report();
    if (ADC_Stale())
    {
      streaming = 0U;
    }
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == ADS_DRDY_Pin)
  {
    ADS1256_DataReady(&adc);
  }
}
