#include "../crypto/sha.hpp"
#include "../crypto/x25519.hpp"
#include "connection.hpp"
#include "cert_verify.hpp"

#include <algorithm>
#include <cstring>
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
// clang-format on

namespace browser::net::tls {

    static std::vector<u8> make_handshake_msg(u8 type, const std::vector<u8> &body) {
        std::vector<u8> msg;
        msg.push_back(type);
        msg.push_back(static_cast<u8>((body.size() >> 16) & 0xFF));
        msg.push_back(static_cast<u8>((body.size() >> 8) & 0xFF));
        msg.push_back(static_cast<u8>(body.size() & 0xFF));
        msg.insert(msg.end(), body.begin(), body.end());
        return msg;
    }

    struct ParsedHS {
        u8 type;
        u32 body_len;
    };

    static bool parse_hs_header(const std::vector<u8> &data, std::size_t offset, ParsedHS &out) {
        if (offset + 4 > data.size())
            return false;
        out.type = data[offset];
        out.body_len = (static_cast<u32>(data[offset + 1]) << 16) | (static_cast<u32>(data[offset + 2]) << 8) |
                       static_cast<u32>(data[offset + 3]);
        return true;
    }

    void TLSConnection::append_handshake_to_transcript(u8 type, const std::vector<u8> &body) {
        auto msg = make_handshake_msg(type, body);
        transcript_.insert(transcript_.end(), msg.begin(), msg.end());
        transcript_hasher_.update(msg.data(), msg.size());
    }

    std::vector<u8> TLSConnection::compute_transcript_hash() const {
        crypto::SHA256 copy(transcript_hasher_);
        return copy.digest();
    }

    std::vector<u8> TLSConnection::hkdf_expand_label(const std::vector<u8> &secret,
                                                     const std::string &label,
                                                     const std::vector<u8> &context,
                                                     u32 length) {
        std::vector<u8> info;
        info.push_back(static_cast<u8>((length >> 8) & 0xFF));
        info.push_back(static_cast<u8>(length & 0xFF));
        std::string full_label = "tls13 " + label;
        info.push_back(static_cast<u8>(full_label.size()));
        info.insert(info.end(), full_label.begin(), full_label.end());
        info.push_back(static_cast<u8>(context.size()));
        info.insert(info.end(), context.begin(), context.end());

        return crypto::HKDF::expand(secret, info, length);
    }

    void TLSConnection::derive_handshake_keys(const std::vector<u8> &shared_secret) {
        std::vector<u8> zero_salt(32, 0);
        std::vector<u8> zero_ikm(32, 0);
        std::vector<u8> early_secret = crypto::HKDF::extract(zero_salt, zero_ikm);

        auto empty_hash = crypto::SHA256::hash((const u8 *)"", 0);
        auto derived = hkdf_expand_label(early_secret, "derived", empty_hash, 32);

        std::vector<u8> hs_secret = crypto::HKDF::extract(derived, shared_secret);

        std::memcpy(handshake_secret_, hs_secret.data(), 32);

        auto hs_hash = compute_transcript_hash();
        client_hs_traffic_ = hkdf_expand_label(hs_secret, "c hs traffic", hs_hash, 32);
        server_hs_traffic_ = hkdf_expand_label(hs_secret, "s hs traffic", hs_hash, 32);

        u32 hs_key_size = (cipher_suite_ == 0x1303) ? 32 : 16;
        auto c_key = hkdf_expand_label(client_hs_traffic_, "key", {}, hs_key_size);
        auto c_iv = hkdf_expand_label(client_hs_traffic_, "iv", {}, 12);
        auto s_key = hkdf_expand_label(server_hs_traffic_, "key", {}, hs_key_size);
        auto s_iv = hkdf_expand_label(server_hs_traffic_, "iv", {}, 12);

        std::memcpy(client_hs_key_, c_key.data(), hs_key_size);
        if (hs_key_size < 32)
            std::memset(client_hs_key_ + hs_key_size, 0, 32 - hs_key_size);
        std::memcpy(client_hs_iv_, c_iv.data(), 12);
        std::memcpy(server_hs_key_, s_key.data(), hs_key_size);
        if (hs_key_size < 32)
            std::memset(server_hs_key_ + hs_key_size, 0, 32 - hs_key_size);
        std::memcpy(server_hs_iv_, s_iv.data(), 12);

        if (cipher_suite_ != 0x1303) {
            aes_encrypt_.set_key(client_hs_key_, 16);
            aes_decrypt_.set_key(server_hs_key_, 16);
        }
    }

    void TLSConnection::derive_application_keys() {
        std::vector<u8> zero_ikm(32, 0);
        std::vector<u8> hs_secret(handshake_secret_, handshake_secret_ + 32);
        auto empty_hash = crypto::SHA256::hash((const u8 *)"", 0);
        auto derived = hkdf_expand_label(hs_secret, "derived", empty_hash, 32);

        std::vector<u8> master_secret = crypto::HKDF::extract(derived, zero_ikm);

        auto hs_hash = compute_transcript_hash();
        client_app_traffic_ = hkdf_expand_label(master_secret, "c ap traffic", hs_hash, 32);
        server_app_traffic_ = hkdf_expand_label(master_secret, "s ap traffic", hs_hash, 32);

        u32 app_key_size = (cipher_suite_ == 0x1303) ? 32 : 16;
        auto c_key = hkdf_expand_label(client_app_traffic_, "key", {}, app_key_size);
        auto c_iv = hkdf_expand_label(client_app_traffic_, "iv", {}, 12);
        auto s_key = hkdf_expand_label(server_app_traffic_, "key", {}, app_key_size);
        auto s_iv = hkdf_expand_label(server_app_traffic_, "iv", {}, 12);

        std::memcpy(client_app_key_, c_key.data(), app_key_size);
        if (app_key_size < 32)
            std::memset(client_app_key_ + app_key_size, 0, 32 - app_key_size);
        std::memcpy(client_app_iv_, c_iv.data(), 12);
        std::memcpy(server_app_key_, s_key.data(), app_key_size);
        if (app_key_size < 32)
            std::memset(server_app_key_ + app_key_size, 0, 32 - app_key_size);
        std::memcpy(server_app_iv_, s_iv.data(), 12);

        if (cipher_suite_ != 0x1303) {
            aes_encrypt_.set_key(client_app_key_, 16);
            aes_decrypt_.set_key(server_app_key_, 16);
        }

        app_keys_set_ = true;
    }

    std::vector<u8> TLSConnection::build_client_hello(const std::string &hostname) {
        crypto::X25519::generate_keypair(client_priv_, client_pub_);

        std::vector<u8> ch;

        ch.push_back(0x03);
        ch.push_back(0x03);

        u8 random[32];
        BCryptGenRandom(nullptr, random, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        ch.insert(ch.end(), random, random + 32);

        ch.push_back(0x00);

        u16 cs_count = 2;
        ch.push_back(static_cast<u8>((cs_count * 2) >> 8));
        ch.push_back(static_cast<u8>((cs_count * 2)));
        ch.push_back(0x13);
        ch.push_back(0x01);
        ch.push_back(0x13);
        ch.push_back(0x03);

        ch.push_back(0x01);
        ch.push_back(0x00);

        std::vector<u8> exts;

        // supported_versions (0x002b)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x2b);
            std::vector<u8> data;
            data.push_back(0x02);
            data.push_back(0x03);
            data.push_back(0x04);
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        // key_share (0x0033)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x33);
            std::vector<u8> entry;
            entry.push_back(0x00);
            entry.push_back(0x1d);
            entry.push_back(0x00);
            entry.push_back(0x20);
            entry.insert(entry.end(), client_pub_, client_pub_ + 32);
            std::vector<u8> data;
            data.push_back(static_cast<u8>((entry.size() >> 8) & 0xFF));
            data.push_back(static_cast<u8>(entry.size() & 0xFF));
            data.insert(data.end(), entry.begin(), entry.end());
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        // signature_algorithms (0x000d)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x0d);
            std::vector<u8> data;
            u8 algs[] = {0x08, 0x04, 0x04, 0x03, 0x08, 0x09, 0x08, 0x0a};
            u16 alg_len = sizeof(algs);
            data.push_back(static_cast<u8>((alg_len >> 8) & 0xFF));
            data.push_back(static_cast<u8>(alg_len & 0xFF));
            data.insert(data.end(), algs, algs + alg_len);
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        // supported_groups (0x000a)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x0a);
            std::vector<u8> data;
            u8 groups[] = {0x00, 0x1d, 0x00, 0x17};
            u16 grp_len = sizeof(groups);
            data.push_back(static_cast<u8>((grp_len >> 8) & 0xFF));
            data.push_back(static_cast<u8>(grp_len & 0xFF));
            data.insert(data.end(), groups, groups + grp_len);
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        // ALPN (0x0010)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x10);
            std::vector<u8> data;
            std::vector<u8> proto;
            proto.push_back(0x02);
            proto.insert(proto.end(), (const u8 *)"h2", (const u8 *)"h2" + 2);
            proto.push_back(0x08);
            proto.insert(proto.end(), (const u8 *)"http/1.1", (const u8 *)"http/1.1" + 8);
            u16 proto_len = static_cast<u16>(proto.size());
            data.push_back(static_cast<u8>((proto_len >> 8) & 0xFF));
            data.push_back(static_cast<u8>(proto_len & 0xFF));
            data.insert(data.end(), proto.begin(), proto.end());
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        // SNI (0x0000)
        {
            std::vector<u8> ext;
            ext.push_back(0x00);
            ext.push_back(0x00);
            std::vector<u8> data;
            std::vector<u8> sni;
            sni.push_back(0x00);
            u16 name_len = static_cast<u16>(hostname.size());
            sni.push_back(static_cast<u8>((name_len >> 8) & 0xFF));
            sni.push_back(static_cast<u8>(name_len & 0xFF));
            sni.insert(sni.end(), hostname.begin(), hostname.end());
            u16 sni_len = static_cast<u16>(sni.size());
            data.push_back(static_cast<u8>((sni_len >> 8) & 0xFF));
            data.push_back(static_cast<u8>(sni_len & 0xFF));
            data.insert(data.end(), sni.begin(), sni.end());
            ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
            ext.push_back(static_cast<u8>(data.size() & 0xFF));
            ext.insert(ext.end(), data.begin(), data.end());
            exts.insert(exts.end(), ext.begin(), ext.end());
        }

        u16 exts_len = static_cast<u16>(exts.size());
        ch.push_back(static_cast<u8>((exts_len >> 8) & 0xFF));
        ch.push_back(static_cast<u8>(exts_len & 0xFF));
        ch.insert(ch.end(), exts.begin(), exts.end());

        return ch;
    }

    Result<void> TLSConnection::connect(Connection *tcp, const std::string &hostname) {
        reset_state();
        tcp_ = tcp;
        u64 deadline = GetTickCount64() + 15000;

        auto ch_body = build_client_hello(hostname);
        append_handshake_to_transcript(HS_CLIENT_HELLO, ch_body);

        auto ch_msg = make_handshake_msg(HS_CLIENT_HELLO, ch_body);
        auto r = send_raw_record(HANDSHAKE, ch_msg);
        if (r.is_err())
            return std::string("send client hello: " + r.unwrap_err());

        if (GetTickCount64() > deadline)
            return std::string("handshake timeout");
        auto sh_r = read_raw_record();
        if (sh_r.is_err())
            return std::string("read server hello: " + sh_r.unwrap_err());
        auto &sh_data = sh_r.unwrap();

        ParsedHS hs;
        if (!parse_hs_header(sh_data, 0, hs))
            return std::string("bad server hello header");
        if (hs.type != HS_SERVER_HELLO)
            return std::string("expected server hello");

        transcript_.insert(transcript_.end(), sh_data.begin(), sh_data.end());
        transcript_hasher_.update(sh_data.data(), sh_data.size());

        auto sh_body = std::vector<u8>(sh_data.begin() + 4, sh_data.end());
        std::size_t off = 0;

        if (off + 2 > sh_body.size())
            return std::string("truncated SH");
        off += 2;

        if (off + 32 > sh_body.size())
            return std::string("truncated SH random");
        off += 32;

        if (off + 1 > sh_body.size())
            return std::string("truncated SH sid");
        u8 sid_len = sh_body[off++];
        if (off + sid_len > sh_body.size())
            return std::string("truncated SH sid2");
        off += sid_len;

        if (off + 2 > sh_body.size())
            return std::string("truncated SH cs");
        cipher_suite_ = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
        off += 2;
        if (cipher_suite_ != 0x1301 && cipher_suite_ != 0x1303)
            return std::string("unsupported cipher suite: " + std::to_string(cipher_suite_));

        if (off + 1 > sh_body.size())
            return std::string("truncated SH comp");
        off++;

        if (off + 2 > sh_body.size())
            return std::string("truncated SH ext");
        u16 sh_exts_len = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
        off += 2;

        if (off + sh_exts_len > sh_body.size())
            return std::string("truncated SH exts2");

        std::vector<u8> server_pub;
        std::size_t ext_off = off;
        std::size_t ext_end = off + sh_exts_len;

        while (ext_off + 4 <= ext_end) {
            u16 ext_type = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
            u16 ext_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
            ext_off += 4;
            if (ext_off + ext_len > ext_end)
                break;

            if (ext_type == 0x0033 && ext_len >= 4) {
                u16 group = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
                u16 key_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
                if (group == 0x001d && key_len == 32 && ext_off + 4 + key_len <= ext_end) {
                    server_pub.assign(sh_body.begin() + ext_off + 4, sh_body.begin() + ext_off + 4 + key_len);
                }
            }
            ext_off += ext_len;
        }

        if (server_pub.size() != 32)
            return std::string("no key_share in SH");

        u8 shared_secret[32];
        crypto::X25519::shared_secret(client_priv_, server_pub.data(), shared_secret);
        std::vector<u8> ss(shared_secret, shared_secret + 32);

        derive_handshake_keys(ss);

        int msgs_needed = 4;
        while (msgs_needed > 0) {
            if (GetTickCount64() > deadline)
                return std::string("handshake timeout");
            auto rec_r = read_encrypted_record(server_hs_key_, server_hs_iv_, server_seq_);
            if (rec_r.is_err())
                return std::string("read hs record: " + rec_r.unwrap_err());
            auto &rec_data = rec_r.unwrap();
            if (rec_data.empty())
                continue;

            std::size_t msg_off = 0;
            while (msg_off < rec_data.size() && msgs_needed > 0) {
                ParsedHS phs;
                if (!parse_hs_header(rec_data, msg_off, phs))
                    return std::string("bad hs header in encrypted record");

                u32 total_len = 4 + phs.body_len;
                if (msg_off + total_len > rec_data.size())
                    return std::string("truncated hs msg");

                auto msg_bytes = std::vector<u8>(rec_data.begin() + msg_off, rec_data.begin() + msg_off + total_len);

                auto body = std::vector<u8>(rec_data.begin() + msg_off + 4, rec_data.begin() + msg_off + total_len);

                if (phs.type == HS_ENCRYPTED_EXTENSIONS) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    std::size_t eo = 0;
                    if (eo + 2 <= body.size()) {
                        u16 ee_exts_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                        eo += 2;
                        std::size_t ee_end = eo + ee_exts_len;
                        while (eo + 4 <= ee_end) {
                            u16 etype = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                            u16 elen = (static_cast<u16>(body[eo + 2]) << 8) | body[eo + 3];
                            eo += 4;
                            if (eo + elen > ee_end)
                                break;
                            if (etype == 0x0010) {
                                u16 list_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                                if (list_len >= 2 && eo + 2 + list_len <= ee_end) {
                                    u8 proto_len = body[eo + 2];
                                    if (proto_len > 0 && proto_len <= list_len - 1) {
                                        alpn_.assign(reinterpret_cast<const char *>(body.data() + eo + 3), proto_len);
                                    }
                                }
                            }
                            eo += elen;
                        }
                    }
                    msgs_needed--;
                } else if (phs.type == HS_CERTIFICATE) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    {
                        peer_certs_.clear();
                        std::size_t off = 0;
                        if (off < body.size()) {
                            u8 ctx_len = body[off++];
                            off += ctx_len;
                            if (off + 3 <= body.size()) {
                                u32 list_len = (static_cast<u32>(body[off]) << 16) |
                                               (static_cast<u32>(body[off + 1]) << 8) |
                                               static_cast<u32>(body[off + 2]);
                                off += 3;
                                std::size_t list_end = off + list_len;
                                while (off + 3 <= list_end) {
                                    u32 cert_len = (static_cast<u32>(body[off]) << 16) |
                                                   (static_cast<u32>(body[off + 1]) << 8) |
                                                   static_cast<u32>(body[off + 2]);
                                    off += 3;
                                    if (off + cert_len + 2 > list_end) break;
                                    std::vector<u8> cert_der(body.begin() + off, body.begin() + off + cert_len);
                                    peer_certs_.push_back(std::move(cert_der));
                                    off += cert_len;
                                    u16 ext_len = (static_cast<u16>(body[off]) << 8) | body[off + 1];
                                    off += 2 + ext_len;
                                }
                            }
                        }
                    }
                    msgs_needed--;
                } else if (phs.type == HS_CERTIFICATE_VERIFY) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    msgs_needed--;
                } else if (phs.type == HS_FINISHED) {
                    auto transcript_hash = compute_transcript_hash();
                    auto finished_key = hkdf_expand_label(server_hs_traffic_, "finished", {}, 32);
                    auto expected_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
                    if (body.size() != expected_verify_data.size() ||
                        std::memcmp(body.data(), expected_verify_data.data(), body.size()) != 0) {
                        return std::string("server finished verification failed");
                    }
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    msgs_needed--;
                } else if (phs.type == HS_CLIENT_HELLO || phs.type == HS_SERVER_HELLO) {
                    return std::string("unexpected hs type");
                }

                msg_off += total_len;
            }
        }

        if (msgs_needed != 0)
            return std::string("missing handshake messages");

        auto transcript_hash = compute_transcript_hash();
        auto finished_key = hkdf_expand_label(client_hs_traffic_, "finished", {}, 32);
        auto finished_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
        std::vector<u8> finished_body(finished_verify_data.begin(), finished_verify_data.end());

        derive_application_keys();

        append_handshake_to_transcript(HS_FINISHED, finished_body);

        auto r2 = send_encrypted_record(HANDSHAKE, finished_body, client_hs_key_, client_hs_iv_, client_seq_);
        if (r2.is_err())
            return std::string("send finished: " + r2.unwrap_err());

        client_seq_ = 0;
        server_seq_ = 0;

        connected_ = true;
        return {};
    }

    async::task<bool> TLSConnection::connect_async(Connection *tcp, const std::string &hostname) {
        reset_state();
        tcp_ = tcp;
        u64 deadline = GetTickCount64() + 15000;

        auto ch_body = build_client_hello(hostname);
        append_handshake_to_transcript(HS_CLIENT_HELLO, ch_body);

        auto ch_msg = make_handshake_msg(HS_CLIENT_HELLO, ch_body);
        auto r = co_await send_raw_record_async(HANDSHAKE, ch_msg);
        if (r.is_err())
            co_return std::string("send client hello: ") + r.unwrap_err();

        if (GetTickCount64() > deadline)
            co_return std::string("handshake timeout");
        auto sh_r = co_await read_raw_record_async();
        if (sh_r.is_err())
            co_return std::string("read server hello: ") + sh_r.unwrap_err();
        auto sh_data = sh_r.unwrap();

        ParsedHS hs;
        if (!parse_hs_header(sh_data, 0, hs))
            co_return std::string("bad server hello header");
        if (hs.type != HS_SERVER_HELLO)
            co_return std::string("expected server hello");

        transcript_.insert(transcript_.end(), sh_data.begin(), sh_data.end());
        transcript_hasher_.update(sh_data.data(), sh_data.size());

        auto sh_body = std::vector<u8>(sh_data.begin() + 4, sh_data.end());
        std::size_t off = 0;

        if (off + 2 > sh_body.size())
            co_return std::string("truncated SH");
        off += 2;
        if (off + 32 > sh_body.size())
            co_return std::string("truncated SH random");
        off += 32;
        if (off + 1 > sh_body.size())
            co_return std::string("truncated SH sid");
        u8 sid_len = sh_body[off++];
        if (off + sid_len > sh_body.size())
            co_return std::string("truncated SH sid2");
        off += sid_len;
        if (off + 2 > sh_body.size())
            co_return std::string("truncated SH cs");
        cipher_suite_ = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
        off += 2;
        if (cipher_suite_ != 0x1301 && cipher_suite_ != 0x1303)
            co_return std::string("unsupported cipher suite: ") + std::to_string(cipher_suite_);
        if (off + 1 > sh_body.size())
            co_return std::string("truncated SH comp");
        off++;
        if (off + 2 > sh_body.size())
            co_return std::string("truncated SH ext");
        u16 sh_exts_len = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
        off += 2;
        if (off + sh_exts_len > sh_body.size())
            co_return std::string("truncated SH exts2");

        std::vector<u8> server_pub;
        std::size_t ext_off = off;
        std::size_t ext_end = off + sh_exts_len;
        while (ext_off + 4 <= ext_end) {
            u16 ext_type = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
            u16 ext_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
            ext_off += 4;
            if (ext_off + ext_len > ext_end)
                break;
            if (ext_type == 0x0033 && ext_len >= 4) {
                u16 group = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
                u16 key_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
                if (group == 0x001d && key_len == 32 && ext_off + 4 + key_len <= ext_end) {
                    server_pub.assign(sh_body.begin() + ext_off + 4, sh_body.begin() + ext_off + 4 + key_len);
                }
            }
            ext_off += ext_len;
        }
        if (server_pub.size() != 32)
            co_return std::string("no key_share in SH");

        u8 shared_secret[32];
        crypto::X25519::shared_secret(client_priv_, server_pub.data(), shared_secret);
        std::vector<u8> ss(shared_secret, shared_secret + 32);
        derive_handshake_keys(ss);

        int msgs_needed = 4;
        while (msgs_needed > 0) {
            if (GetTickCount64() > deadline)
                co_return std::string("handshake timeout");
            auto rec_r = co_await read_encrypted_record_async(server_hs_key_, server_hs_iv_, server_seq_);
            if (rec_r.is_err())
                co_return std::string("read hs: ") + rec_r.unwrap_err();
            auto rec_data = rec_r.unwrap();
            if (rec_data.empty())
                continue;

            std::size_t msg_off = 0;
            while (msg_off < rec_data.size() && msgs_needed > 0) {
                ParsedHS phs;
                if (!parse_hs_header(rec_data, msg_off, phs))
                    co_return std::string("bad hs header in encrypted record");

                u32 total_len = 4 + phs.body_len;
                if (msg_off + total_len > rec_data.size())
                    co_return std::string("truncated hs msg");

                auto msg_bytes = std::vector<u8>(rec_data.begin() + msg_off, rec_data.begin() + msg_off + total_len);
                auto body = std::vector<u8>(rec_data.begin() + msg_off + 4, rec_data.begin() + msg_off + total_len);

                if (phs.type == HS_ENCRYPTED_EXTENSIONS) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    std::size_t eo = 0;
                    if (eo + 2 <= body.size()) {
                        u16 ee_exts_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                        eo += 2;
                        std::size_t ee_end = eo + ee_exts_len;
                        while (eo + 4 <= ee_end) {
                            u16 etype = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                            u16 elen = (static_cast<u16>(body[eo + 2]) << 8) | body[eo + 3];
                            eo += 4;
                            if (eo + elen > ee_end)
                                break;
                            if (etype == 0x0010) {
                                u16 list_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                                if (list_len >= 2 && eo + 2 + list_len <= ee_end) {
                                    u8 proto_len = body[eo + 2];
                                    if (proto_len > 0 && proto_len <= list_len - 1) {
                                        alpn_.assign(reinterpret_cast<const char *>(body.data() + eo + 3), proto_len);
                                    }
                                }
                            }
                            eo += elen;
                        }
                    }
                    msgs_needed--;
                } else if (phs.type == HS_CERTIFICATE) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    {
                        peer_certs_.clear();
                        std::size_t off = 0;
                        if (off < body.size()) {
                            u8 ctx_len = body[off++];
                            off += ctx_len;
                            if (off + 3 <= body.size()) {
                                u32 list_len = (static_cast<u32>(body[off]) << 16) |
                                               (static_cast<u32>(body[off + 1]) << 8) |
                                               static_cast<u32>(body[off + 2]);
                                off += 3;
                                std::size_t list_end = off + list_len;
                                while (off + 3 <= list_end) {
                                    u32 cert_len = (static_cast<u32>(body[off]) << 16) |
                                                   (static_cast<u32>(body[off + 1]) << 8) |
                                                   static_cast<u32>(body[off + 2]);
                                    off += 3;
                                    if (off + cert_len + 2 > list_end) break;
                                    std::vector<u8> cert_der(body.begin() + off, body.begin() + off + cert_len);
                                    peer_certs_.push_back(std::move(cert_der));
                                    off += cert_len;
                                    u16 ext_len = (static_cast<u16>(body[off]) << 8) | body[off + 1];
                                    off += 2 + ext_len;
                                }
                            }
                        }
                    }
                    msgs_needed--;
                } else if (phs.type == HS_CERTIFICATE_VERIFY) {
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    msgs_needed--;
                } else if (phs.type == HS_FINISHED) {
                    auto transcript_hash = compute_transcript_hash();
                    auto finished_key = hkdf_expand_label(server_hs_traffic_, "finished", {}, 32);
                    auto expected_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
                    if (body.size() != expected_verify_data.size() ||
                        std::memcmp(body.data(), expected_verify_data.data(), body.size()) != 0) {
                        co_return std::string("server finished verification failed");
                    }
                    transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                    transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                    msgs_needed--;
                } else if (phs.type == HS_CLIENT_HELLO || phs.type == HS_SERVER_HELLO) {
                    co_return std::string("unexpected hs type");
                }
                msg_off += total_len;
            }
        }

        if (msgs_needed != 0)
            co_return std::string("missing handshake messages");

        auto transcript_hash = compute_transcript_hash();
        auto finished_key = hkdf_expand_label(client_hs_traffic_, "finished", {}, 32);
        auto finished_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
        std::vector<u8> finished_body(finished_verify_data.begin(), finished_verify_data.end());

        derive_application_keys();
        append_handshake_to_transcript(HS_FINISHED, finished_body);

        auto r2 =
            co_await send_encrypted_record_async(HANDSHAKE, finished_body, client_hs_key_, client_hs_iv_, client_seq_);
        if (r2.is_err())
            co_return std::string("send finished: ") + r2.unwrap_err();

        client_seq_ = 0;
        server_seq_ = 0;

        connected_ = true;
        co_return true;
    }

}  // namespace browser::net::tls
