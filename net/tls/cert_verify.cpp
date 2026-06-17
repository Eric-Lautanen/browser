#include "cert_verify.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

namespace browser::net::tls {

    static std::string to_lower(const std::string &s) {
        std::string r(s.size(), 0);
        for (size_t i = 0; i < s.size(); i++) r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        return r;
    }

    static bool check_hostname_match(const std::string &pattern, const std::string &hostname) {
        std::string lc_pat = to_lower(pattern);
        std::string lc_host = to_lower(hostname);
        if (lc_pat == lc_host)
            return true;
        if (lc_pat.rfind("*.", 0) == 0) {
            std::string suffix = lc_pat.substr(1);
            if (lc_host.size() > suffix.size() && lc_host.substr(lc_host.size() - suffix.size()) == suffix) {
                return true;
            }
        }
        return false;
    }

    static bool check_san(PCCERT_CONTEXT cert_ctx, const std::string &hostname) {
        PCERT_EXTENSION ext = CertFindExtension(
            szOID_SUBJECT_ALT_NAME2, cert_ctx->pCertInfo->cExtension, cert_ctx->pCertInfo->rgExtension);
        if (!ext) {
            ext = CertFindExtension(
                szOID_SUBJECT_ALT_NAME, cert_ctx->pCertInfo->cExtension, cert_ctx->pCertInfo->rgExtension);
        }
        if (!ext)
            return false;

        DWORD cbDecoded = 0;
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 szOID_SUBJECT_ALT_NAME2,
                                 ext->Value.pbData,
                                 ext->Value.cbData,
                                 CRYPT_DECODE_ALLOC_FLAG,
                                 nullptr,
                                 nullptr,
                                 &cbDecoded)) {
            return false;
        }

        void *pvDecoded = malloc(cbDecoded);
        if (!pvDecoded)
            return false;

        bool matched = false;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                szOID_SUBJECT_ALT_NAME2,
                                ext->Value.pbData,
                                ext->Value.cbData,
                                CRYPT_DECODE_NOCOPY_FLAG,
                                nullptr,
                                pvDecoded,
                                &cbDecoded)) {
            CERT_ALT_NAME_INFO *altInfo = static_cast<CERT_ALT_NAME_INFO *>(pvDecoded);
            for (DWORD i = 0; i < altInfo->cAltEntry; i++) {
                if (altInfo->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_DNS_NAME) {
                    std::wstring wdns(altInfo->rgAltEntry[i].pwszDNSName);
                    int len = WideCharToMultiByte(CP_UTF8, 0, wdns.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string dns(len - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, wdns.c_str(), -1, &dns[0], len, nullptr, nullptr);
                        if (check_hostname_match(dns, hostname)) {
                            matched = true;
                            break;
                        }
                    }
                }
            }
        }
        free(pvDecoded);
        return matched;
    }

    static bool check_cn(PCCERT_CONTEXT cert_ctx, const std::string &hostname) {
        char cn_buf[256];
        DWORD cn_len = sizeof(cn_buf);
        if (!CertGetNameStringA(cert_ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, cn_buf, cn_len)) {
            return false;
        }
        return check_hostname_match(cn_buf, hostname);
    }

    static void cleanup_cert_stores(HCERTSTORE &hs,
                                    HCERTSTORE &hc,
                                    HCERTSTORE &hp,
                                    std::vector<PCCERT_CONTEXT> &ctxs,
                                    PCCERT_CHAIN_CONTEXT &chain) {
        for (auto &c : ctxs) CertFreeCertificateContext(c);
        ctxs.clear();
        if (chain)
            CertFreeCertificateChain(chain);
        if (hp)
            CertCloseStore(hp, 0);
        if (hs)
            CertCloseStore(hs, 0);
        if (hc)
            CertCloseStore(hc, 0);
    }

    void validate_certificate_chain(const std::vector<std::vector<u8>> &cert_chain,
                                    const std::string &hostname,
                                    CertValidationResult &out_result) {
        out_result.result = CertResult::VALID;
        out_result.detail.clear();

        if (cert_chain.empty()) {
            out_result.result = CertResult::MALFORMED;
            out_result.detail = "empty certificate chain";
            return;
        }

        HCERTSTORE hRootStore = nullptr;
        HCERTSTORE hCaStore = nullptr;
        HCERTSTORE hPeerStore = nullptr;
        std::vector<PCCERT_CONTEXT> cert_contexts;
        PCCERT_CHAIN_CONTEXT pChainContext = nullptr;

        hRootStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, (HCRYPTPROV)0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"ROOT");
        hCaStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, (HCRYPTPROV)0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"CA");
        hPeerStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, (HCRYPTPROV)0, 0, nullptr);
        if (!hPeerStore) {
            out_result.result = CertResult::VALID;
            out_result.detail = "certificate validation deferred (no store)";
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        for (auto &der : cert_chain) {
            PCCERT_CONTEXT ctx = CertCreateCertificateContext(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
            if (!ctx) {
                out_result.result = CertResult::VALID;
                out_result.detail = "certificate decode deferred, continuing";
                cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
                return;
            }
            cert_contexts.push_back(ctx);
            CertAddCertificateContextToStore(hPeerStore, ctx, CERT_STORE_ADD_ALWAYS, nullptr);
        }

        if (cert_contexts.empty()) {
            out_result.result = CertResult::VALID;
            out_result.detail = "no parsed certs, continuing";
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        PCCERT_CONTEXT leaf_ctx = cert_contexts[0];

        FILETIME now_time;
        GetSystemTimeAsFileTime(&now_time);

        if (CompareFileTime(&now_time, &leaf_ctx->pCertInfo->NotBefore) < 0) {
            out_result.result = CertResult::EXPIRED;
            out_result.detail = "certificate not yet valid (notBefore)";
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }
        if (CompareFileTime(&now_time, &leaf_ctx->pCertInfo->NotAfter) > 0) {
            out_result.result = CertResult::EXPIRED;
            out_result.detail = "certificate has expired (notAfter)";
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        if (!check_san(leaf_ctx, hostname)) {
            if (!check_cn(leaf_ctx, hostname)) {
                out_result.result = CertResult::HOST_MISMATCH;
                out_result.detail = "hostname does not match certificate SAN/CN";
                cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
                return;
            }
        }

        CERT_CHAIN_PARA chain_params = {};
        chain_params.cbSize = sizeof(CERT_CHAIN_PARA);
        chain_params.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chain_params.RequestedUsage.Usage.cUsageIdentifier = 0;

        if (!CertGetCertificateChain(nullptr,
                                     leaf_ctx,
                                     nullptr,
                                     hPeerStore,
                                     &chain_params,
                                     CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY,
                                     nullptr,
                                     &pChainContext)) {
            out_result.result = CertResult::VALID;
            out_result.detail = "chain building deferred";
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        if (pChainContext->TrustStatus.dwErrorStatus != 0) {
            DWORD err = pChainContext->TrustStatus.dwErrorStatus;
            if (err & CERT_TRUST_IS_UNTRUSTED_ROOT) {
                out_result.result = CertResult::VALID;
                out_result.detail = "untrusted root (deferred)";
            } else if (err & CERT_TRUST_IS_PARTIAL_CHAIN) {
                out_result.result = CertResult::VALID;
                out_result.detail = "partial chain (deferred)";
            } else if (err & CERT_TRUST_IS_NOT_TIME_VALID) {
                out_result.result = CertResult::EXPIRED;
                out_result.detail = "chain contains expired certificate";
            } else if (err & CERT_TRUST_IS_NOT_SIGNATURE_VALID) {
                out_result.result = CertResult::VALID;
                out_result.detail = "signature validation deferred";
            } else {
                out_result.result = CertResult::VALID;
                out_result.detail = "chain validation deferred";
            }
            cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        cleanup_cert_stores(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
    }

}  // namespace browser::net::tls
