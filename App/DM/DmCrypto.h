#ifndef __DM_CRYPTO_H__
#define __DM_CRYPTO_H__

#include <string>

namespace dm
{

int DmEncryptValue(const std::string& secret,
                   const std::string& plaintext,
                   std::string& encrypted);

}

#endif
