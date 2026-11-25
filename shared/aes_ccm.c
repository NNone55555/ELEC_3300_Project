#include "aes_ccm.h"
#include "bearssl.h"

void Crypto_Clear(void *data, size_t length)
{
  volatile uint8_t *p = data;
  while (length--)
    *p++ = 0;
}

static bool Run(const uint8_t *key, const uint8_t *nonce, const void *aad, size_t aad_len,
                uint8_t *data, size_t data_len, uint8_t *out_tag, const uint8_t *in_tag)
{
  br_aes_ct_ctrcbc_keys aes;
  br_ccm_context ccm;
  br_aes_ct_ctrcbc_init(&aes, key, AES_CCM_KEY_BYTES);
  br_ccm_init(&ccm, &aes.vtable);
  bool ok = br_ccm_reset(&ccm, nonce, AES_CCM_NONCE_BYTES, aad_len, data_len, AES_CCM_TAG_BYTES) != 0;
  if (ok)
  {
    if (aad_len != 0)
      br_ccm_aad_inject(&ccm, aad, aad_len);
    br_ccm_flip(&ccm);
    br_ccm_run(&ccm, out_tag != NULL, data, data_len);
    if (out_tag != NULL)
      br_ccm_get_tag(&ccm, out_tag);
    else
      ok = br_ccm_check_tag(&ccm, in_tag) != 0;
  }
  Crypto_Clear(&ccm, sizeof(ccm));
  Crypto_Clear(&aes, sizeof(aes));
  if (!ok)
    Crypto_Clear(data, data_len);
  return ok;
}

bool AesCcm_Seal(const uint8_t key[16], const uint8_t nonce[12], const void *aad, size_t aad_len,
                 uint8_t *data, size_t data_len, uint8_t tag[8])
{
  return Run(key, nonce, aad, aad_len, data, data_len, tag, NULL);
}

bool AesCcm_Open(const uint8_t key[16], const uint8_t nonce[12], const void *aad, size_t aad_len,
                 uint8_t *data, size_t data_len, const uint8_t tag[8])
{
  return Run(key, nonce, aad, aad_len, data, data_len, NULL, tag);
}
