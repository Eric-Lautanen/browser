#pragma once
#include "../../tests/utility.hpp"

#include <string>
#include <vector>

namespace browser::net::tls {

    enum class CertResult { VALID, EXPIRED, HOST_MISMATCH, UNTRUSTED, MALFORMED };

    struct CertValidationResult {
        CertResult result = CertResult::VALID;
        std::string detail;
        bool is_valid() const { return result == CertResult::VALID; }
    };

    void validate_certificate_chain(const std::vector<std::vector<u8>> &cert_chain,
                                    const std::string &hostname,
                                    CertValidationResult &out_result);

}  // namespace browser::net::tls
