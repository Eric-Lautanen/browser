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

    enum class CVResult { VALID, INVALID_SIGNATURE, UNSUPPORTED_ALGORITHM, NO_PUBLIC_KEY };

    enum class PubKeyKind { NONE, RSA, EC_P256 };

    struct PublicKey {
        PubKeyKind kind = PubKeyKind::NONE;
        std::vector<u8> n;      // RSA modulus (big-endian)
        std::vector<u8> point;  // EC: 0x04 || X(32) || Y(32)
    };

    // Extracts the SubjectPublicKeyInfo public key from a DER certificate.
    bool extract_certificate_public_key(const std::vector<u8> &cert, PublicKey &out);

    // Content covered by a TLS 1.3 CertificateVerify signature (RFC 8446 section 4.4.3):
    // 64 spaces || context string || 0x00 || transcript_hash.
    std::vector<u8> make_certificate_verify_content(const std::string &context, const std::vector<u8> &transcript_hash);

    // Verifies a TLS 1.3 server CertificateVerify signature over the transcript hash
    // using the leaf certificate's public key (N-S1). Supports rsa_pss_rsae_sha256
    // (0x0804) and ecdsa_secp256r1_sha256 (0x0403).
    CVResult verify_server_certificate_verify(const std::vector<std::vector<u8>> &cert_chain,
                                              u16 sig_alg,
                                              const std::vector<u8> &signature,
                                              const std::vector<u8> &transcript_hash);

}  // namespace browser::net::tls
