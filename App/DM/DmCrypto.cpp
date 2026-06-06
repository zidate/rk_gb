#include "DmCrypto.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#ifdef DM_CRYPTO_USE_FFMPEG
extern "C" {
#include "libavutil/aes.h"
#include "libavutil/base64.h"
#include "libavutil/mem.h"
#include "libavutil/sha.h"
}
#else
#include <openssl/evp.h>
#include <openssl/sha.h>
#endif

namespace dm
{
namespace
{

static const char* const kDmAesFaqPlain = "123456";
static const char* const kDmAesFaqSecret = "a1adfef9ed014286b7f7a314ee978f15";
static const char* const kDmAesFaqCipher = "maC2/b2Vi517QalT6Ebeyg==";

std::string Base64Encode(const std::vector<uint8_t>& input)
{
    static const char* const alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t b0 = input[i];
        const uint32_t b1 = (i + 1 < input.size()) ? input[i + 1] : 0;
        const uint32_t b2 = (i + 2 < input.size()) ? input[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(alphabet[(triple >> 18) & 0x3F]);
        out.push_back(alphabet[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < input.size()) ? alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < input.size()) ? alphabet[triple & 0x3F] : '=');
    }

    return out;
}

void Pkcs7Pad(const std::string& plaintext, std::vector<uint8_t>& out)
{
    out.assign(plaintext.begin(), plaintext.end());
    size_t pad = 16 - (out.size() % 16);
    if (pad == 0) {
        pad = 16;
    }
    out.insert(out.end(), pad, static_cast<uint8_t>(pad));
}

#ifdef DM_CRYPTO_USE_FFMPEG
int Sha256Key(const std::string& secret, uint8_t out[32])
{
    AVSHA* sha = av_sha_alloc();
    if (sha == NULL) {
        return -1;
    }
    if (av_sha_init(sha, 256) != 0) {
        av_free(sha);
        return -2;
    }
    av_sha_update(sha,
                  reinterpret_cast<const uint8_t*>(secret.data()),
                  static_cast<unsigned int>(secret.size()));
    av_sha_final(sha, out);
    av_free(sha);
    return 0;
}

int Aes256CbcEncrypt(const uint8_t key[32],
                     const std::vector<uint8_t>& padded,
                     std::vector<uint8_t>& cipher)
{
    AVAES* aes = av_aes_alloc();
    if (aes == NULL) {
        return -1;
    }
    if (av_aes_init(aes, key, 256, 0) != 0) {
        av_free(aes);
        return -2;
    }

    cipher.assign(padded.size(), 0);
    uint8_t iv[16] = {0};
    av_aes_crypt(aes,
                 cipher.empty() ? NULL : &cipher[0],
                 padded.empty() ? NULL : &padded[0],
                 static_cast<int>(padded.size() / 16),
                 iv,
                 0);
    av_free(aes);
    return 0;
}
#else
int Sha256Key(const std::string& secret, uint8_t out[32])
{
    // DM requires SHA256(secret) as the AES-256-CBC key.
    SHA256(reinterpret_cast<const unsigned char*>(secret.data()),
           secret.size(),
           out);
    return 0;
}

int Aes256CbcEncrypt(const uint8_t key[32],
                     const std::vector<uint8_t>& padded,
                     std::vector<uint8_t>& cipher)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        return -1;
    }

    uint8_t iv[16] = {0};
    int ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -2;
    }

    EVP_CIPHER_CTX_set_padding(ctx, 0);
    cipher.assign(padded.size() + 16, 0);
    int outLen = 0;
    int total = 0;
    ret = EVP_EncryptUpdate(ctx,
                            cipher.empty() ? NULL : &cipher[0],
                            &outLen,
                            padded.empty() ? NULL : &padded[0],
                            static_cast<int>(padded.size()));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -3;
    }
    total += outLen;

    ret = EVP_EncryptFinal_ex(ctx, &cipher[0] + total, &outLen);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -4;
    }
    total += outLen;
    cipher.resize(total);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}
#endif

}

int DmEncryptValue(const std::string& secret,
                   const std::string& plaintext,
                   std::string& encrypted)
{
    encrypted.clear();
    if (secret.empty()) {
        return -1;
    }

    uint8_t key[32] = {0};
    int ret = Sha256Key(secret, key);
    if (ret != 0) {
        return ret;
    }

    std::vector<uint8_t> padded;
    Pkcs7Pad(plaintext, padded);

    std::vector<uint8_t> cipher;
    ret = Aes256CbcEncrypt(key, padded, cipher);
    if (ret != 0) {
        return ret;
    }

    encrypted = Base64Encode(cipher);

    // Keep the official DM FAQ vector visible for regression tests:
    // key=a1adfef9ed014286b7f7a314ee978f15, plain=123456,
    // AES-256-CBC result=maC2/b2Vi517QalT6Ebeyg==
    (void)kDmAesFaqPlain;
    (void)kDmAesFaqSecret;
    (void)kDmAesFaqCipher;
    return 0;
}

}
