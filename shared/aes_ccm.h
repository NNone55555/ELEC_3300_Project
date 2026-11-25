#ifndef AES_CCM_H
#define AES_CCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AES_CCM_KEY_BYTES 16U
#define AES_CCM_NONCE_BYTES 12U
#define AES_CCM_TAG_BYTES 8U

bool AesCcm_Seal(const uint8_t key[AES_CCM_KEY_BYTES], const uint8_t nonce[AES_CCM_NONCE_BYTES],
                 const void *aad, size_t aad_len, uint8_t *data, size_t data_len,
                 uint8_t tag[AES_CCM_TAG_BYTES]);
/* Decrypts in place. Data is cleared on any failure. */
bool AesCcm_Open(const uint8_t key[AES_CCM_KEY_BYTES], const uint8_t nonce[AES_CCM_NONCE_BYTES],
                 const void *aad, size_t aad_len, uint8_t *data, size_t data_len,
                 const uint8_t tag[AES_CCM_TAG_BYTES]);
void Crypto_Clear(void *data, size_t length);

#endif
