#include "boot_epoch.h"
#include "stm32f1xx_hal.h"

#define PAGE_BYTES 2048U
#define FIRST_PAGE 0x0807F000U
#define ENTRY_WORDS 9U /* Four value halfwords, four inverse halfwords, commit. */
#define ENTRY_BYTES (ENTRY_WORDS * sizeof(uint16_t))
#define ENTRIES_PER_PAGE (PAGE_BYTES / ENTRY_BYTES)
#define MAX_EPOCH (UINT64_MAX >> 2)

static uint64_t boot_epoch;

static const volatile uint16_t *Entry(unsigned page, unsigned index)
{
  return (const volatile uint16_t *)(uintptr_t)(FIRST_PAGE + page * PAGE_BYTES + index * ENTRY_BYTES);
}

static uint64_t ReadEntry(const volatile uint16_t *entry)
{
  if (entry[8] != 0U)
    return 0;
  uint64_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
  {
    uint16_t word = entry[i];
    if ((uint16_t)(word ^ entry[i + 4U]) != UINT16_MAX)
      return 0;
    value |= (uint64_t)word << (16U * i);
  }
  return value <= MAX_EPOCH ? value : 0;
}

static bool Erased(const volatile uint16_t *entry)
{
  for (unsigned i = 0; i < ENTRY_WORDS; ++i)
    if (entry[i] != UINT16_MAX)
      return false;
  return true;
}

static bool WriteEntry(const volatile uint16_t *entry, uint64_t value)
{
  uint32_t address = (uint32_t)(uintptr_t)entry;
  for (unsigned i = 0; i < 8U; ++i)
  {
    uint16_t word = (uint16_t)(value >> (16U * (i % 4U)));
    if (i >= 4U)
      word = (uint16_t)~word;
    if (word != UINT16_MAX && HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + 2U * i, word) != HAL_OK)
      return false;
    if (entry[i] != word)
      return false;
  }
  return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + 16U, 0U) == HAL_OK
         && ReadEntry(entry) == value;
}

bool BootEpoch_Reserve(void)
{
  if (boot_epoch != 0)
    return true;
  uint64_t latest = 0;
  unsigned page = 0;
  for (unsigned p = 0; p < 2U; ++p)
    for (unsigned i = 0; i < ENTRIES_PER_PAGE; ++i)
    {
      uint64_t value = ReadEntry(Entry(p, i));
      if (value > latest)
      {
        latest = value;
        page = p;
      }
    }
  if (latest == MAX_EPOCH || HAL_FLASH_Unlock() != HAL_OK)
    return false;

  unsigned slot = 0;
  while (slot < ENTRIES_PER_PAGE && !Erased(Entry(page, slot)))
    ++slot;
  bool ok = true;
  if (slot == ENTRIES_PER_PAGE)
  {
    /* Preserve the latest committed epoch in the other page during erase. */
    page ^= 1U;
    slot = 0;
    FLASH_EraseInitTypeDef erase = {
      .TypeErase = FLASH_TYPEERASE_PAGES, .PageAddress = FIRST_PAGE + page * PAGE_BYTES, .NbPages = 1U
    };
    uint32_t error;
    ok = HAL_FLASHEx_Erase(&erase, &error) == HAL_OK && Erased(Entry(page, 0));
  }
  ok = ok && WriteEntry(Entry(page, slot), latest + 1U);
  (void)HAL_FLASH_Lock();
  if (ok)
    boot_epoch = latest + 1U;
  return ok;
}

uint64_t BootEpoch_Value(void)
{
  return boot_epoch;
}
