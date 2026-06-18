#include "shaper.hpp"

#include <algorithm>
#include <cstring>

namespace browser::render {

    using internal::read_i16_be;
    using internal::read_u16_be;
    using internal::read_u32_be;

    // ── Unicode joining type ranges ──────────────────────────────────────────
    // Covers Arabic block + extensions. R=Right,D=Dual,U=Non-joining,T=Transparent

    const Shaper::JoiningRange Shaper::joining_ranges[] = {
        // Arabic block U+0600-06FF
        {0x0600, 0x0603, JOIN_U},  // Arabic number sign etc.
        {0x0608, 0x0608, JOIN_U},  // Arabic ray
        {0x060B, 0x060B, JOIN_U},  // Afghani sign
        {0x060D, 0x060D, JOIN_U},  // Arabic date separator
        {0x0610, 0x061A, JOIN_T},  // Arabic sign (marks)
        {0x061E, 0x061E, JOIN_U},  // Arabic triple dot
        {0x061F, 0x061F, JOIN_U},  // Arabic question mark
        {0x0620, 0x0620, JOIN_D},  // Arabic letter Kashmiri Yeh
        {0x0621, 0x0621, JOIN_U},  // Hamza
        {0x0622, 0x0625, JOIN_R},  // Alef with madd/hamza/wasla
        {0x0626, 0x0626, JOIN_D},  // Yeh with hamza
        {0x0627, 0x0627, JOIN_R},  // Alef
        {0x0628, 0x0628, JOIN_D},  // Beh
        {0x0629, 0x0629, JOIN_R},  // Teh marbuta
        {0x062A, 0x062E, JOIN_D},  // Teh-Theh-Khah
        {0x062F, 0x0632, JOIN_R},  // Dal-Dhal-Ra-Zay
        {0x0633, 0x063A, JOIN_D},  // Seen-Sheen-Sad-Dad-Tah-Zah-Ain-Ghain
        {0x063B, 0x063C, JOIN_D},  // Extended
        {0x063D, 0x063F, JOIN_R},  // Extended (Farsi)
        {0x0640, 0x0640, JOIN_C},  // Tatweel
        {0x0641, 0x0648, JOIN_D},  // Feh-Qaf-Kaf-Lam-Meem-Noon-Heh-Waw
        {0x0649, 0x064A, JOIN_D},  // Alef maksura-Yeh
        {0x064B, 0x065F, JOIN_T},  // Combining marks (Fatha-Kasra-Damma-Shadda-Sukun)
        {0x0660, 0x0669, JOIN_U},  // Arabic-Indic digits
        {0x066A, 0x066D, JOIN_U},  // Punctuation
        {0x066E, 0x066E, JOIN_D},  // Arabic letter Dotless Beh
        {0x066F, 0x066F, JOIN_D},  // Arabic letter Dotless Qaf
        {0x0670, 0x0670, JOIN_T},  // Arabic letter superscript Alef
        {0x0671, 0x0673, JOIN_R},  // Alef variants
        {0x0674, 0x0674, JOIN_U},  // Arabic letter high hamza
        {0x0675, 0x0677, JOIN_R},  // High hamza + Alef/Waw
        {0x0678, 0x0678, JOIN_D},  // High hamza Yeh
        {0x0679, 0x067E, JOIN_D},  // Teh-Theh variants (Farsi)
        {0x067F, 0x0680, JOIN_D},  // Farsi variants
        {0x0681, 0x0682, JOIN_D},
        {0x0683, 0x0684, JOIN_D},
        {0x0685, 0x0686, JOIN_D},
        {0x0687, 0x0687, JOIN_D},
        {0x0688, 0x0690, JOIN_R},  // Dal-Reh variants
        {0x0691, 0x0696, JOIN_R},
        {0x0697, 0x0697, JOIN_R},
        {0x0698, 0x0698, JOIN_R},
        {0x0699, 0x0699, JOIN_R},
        {0x069A, 0x06A9, JOIN_D},  // Seen-Sheen-Sad-Dad variants
        {0x06AA, 0x06AF, JOIN_D},  // Kaf variants
        {0x06B0, 0x06B2, JOIN_D},
        {0x06B3, 0x06B5, JOIN_D},
        {0x06B6, 0x06BA, JOIN_D},
        {0x06BB, 0x06BB, JOIN_D},  // Dotless Yeh
        {0x06BC, 0x06BD, JOIN_D},
        {0x06BE, 0x06BE, JOIN_D},  // Knotted Heh
        {0x06BF, 0x06C0, JOIN_D},
        {0x06C1, 0x06C2, JOIN_D},  // Heh goal
        {0x06C3, 0x06C3, JOIN_D},
        {0x06C4, 0x06C5, JOIN_R},
        {0x06C6, 0x06C6, JOIN_R},
        {0x06C7, 0x06C8, JOIN_R},
        {0x06C9, 0x06C9, JOIN_R},
        {0x06CA, 0x06CB, JOIN_R},
        {0x06CC, 0x06CC, JOIN_D},  // Farsi Yeh
        {0x06CD, 0x06CE, JOIN_D},
        {0x06CF, 0x06CF, JOIN_R},
        {0x06D0, 0x06D1, JOIN_D},
        {0x06D2, 0x06D3, JOIN_R},
        {0x06D5, 0x06D5, JOIN_R},  // Ae
        {0x06D6, 0x06DC, JOIN_T},  // Small high marks
        {0x06DD, 0x06DD, JOIN_U},
        {0x06DF, 0x06E4, JOIN_T},  // Marks
        {0x06E5, 0x06E6, JOIN_U},  // Small waw/yeh
        {0x06E7, 0x06E8, JOIN_T},  // Small high marks
        {0x06EA, 0x06ED, JOIN_T},  // Small low marks
        {0x06EE, 0x06EF, JOIN_R},  // Dal/Zay-like
        {0x06F0, 0x06F9, JOIN_U},  // Extended Arabic-Indic digits
        {0x06FA, 0x06FC, JOIN_D},
        {0x06FE, 0x06FE, JOIN_D},
        {0x06FF, 0x06FF, JOIN_D},
        // Arabic Supplement U+0750-077F
        {0x0750, 0x0762, JOIN_D},
        {0x0763, 0x0768, JOIN_D},
        {0x0769, 0x0769, JOIN_D},
        {0x076A, 0x076D, JOIN_D},
        {0x076E, 0x076F, JOIN_R},
        {0x0770, 0x0770, JOIN_R},
        {0x0771, 0x0771, JOIN_D},
        {0x0772, 0x0773, JOIN_R},
        {0x0774, 0x0774, JOIN_U},
        {0x0775, 0x077A, JOIN_D},
        {0x077B, 0x077E, JOIN_D},
        {0x077F, 0x077F, JOIN_D},
        // Arabic Extended-A U+08A0-08FF
        {0x08A0, 0x08AC, JOIN_D},
        {0x08AD, 0x08AD, JOIN_U},
        {0x08AE, 0x08AF, JOIN_R},
        {0x08B0, 0x08B3, JOIN_D},
        {0x08B4, 0x08B4, JOIN_D},
        {0x08B6, 0x08BD, JOIN_D},
        {0x08BE, 0x08BF, JOIN_D},
        {0x08D0, 0x08E2, JOIN_T},  // Combining marks
        {0x08E3, 0x08FF, JOIN_T},  // Combining marks
        // Syriac (some chars have joining behavior)
        {0x0710, 0x0710, JOIN_U},
        // ZWJ
        {0x200D, 0x200D, JOIN_C},
        // General punctuation (U+2000-206F) — treat as non-joining
        // Non-breaking space, soft hyphen, etc.
    };

    const u32 Shaper::joining_range_count = sizeof(joining_ranges) / sizeof(joining_ranges[0]);

    Shaper::JoiningType Shaper::get_joining_type(char32_t cp) {
        for (u32 i = 0; i < joining_range_count; i++) {
            if (cp >= joining_ranges[i].lo && cp <= joining_ranges[i].hi)
                return joining_ranges[i].jt;
        }
        return JOIN_U;
    }

    bool Shaper::is_arabic_script(char32_t cp) {
        return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF) ||
               (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF) || cp == 0x200D;
    }

    bool Shaper::is_transparent(char32_t cp) {
        JoiningType jt = get_joining_type(cp);
        return jt == JOIN_T || (cp >= 0xFE00 && cp <= 0xFE0F) ||  // variation selectors
               cp == 0x200C || cp == 0x200D;                      // ZWNJ, ZWJ
    }

    Shaper::Shaper() = default;

    // ── OpenType Layout Table Helpers ────────────────────────────────────────

    // Pack a 4-byte tag into a u32 for comparison
    static u32 pack_tag(const char tag[4]) {
        return ((u32)(u8)tag[0] << 24) | ((u32)(u8)tag[1] << 16) | ((u32)(u8)tag[2] << 8) | ((u32)(u8)tag[3]);
    }

    // ── GSUB/GPOS parsing ────────────────────────────────────────────────────

    Shaper::ShapingTables *Shaper::get_or_parse(FontFace *face) {
        if (!face)
            return nullptr;
        u32 fid = face->font_id();
        auto it = cache_.find(fid);
        if (it != cache_.end())
            return &it->second;

        if (cache_.size() >= 128)
            cache_.clear();

        ShapingTables t;
        t.gsub_off = face->gsub_off();
        t.gsub_len = face->gsub_len();
        t.gpos_off = face->gpos_off();
        t.gpos_len = face->gpos_len();
        t.gdef_off = face->gdef_off();
        t.gdef_len = face->gdef_len();

        if (t.gsub_len > 0 || t.gpos_len > 0) {
            const u8 *data = face->font_data_ptr();
            u32 size = face->font_data_size();
            parse_gsub_gpos(t, data, size);
        }
        t.parsed = true;
        auto [it2, _] = cache_.emplace(fid, std::move(t));
        return &it2->second;
    }

    void Shaper::parse_gsub_gpos(ShapingTables &t, const u8 *data, u32 size) {
        // Parse GSUB
        if (t.gsub_off && t.gsub_len >= 8) {
            u32 off = t.gsub_off;
            u16 major = read_u16_be(data, off, size);
            (void)major;
            t.gsub_script_list_off = off + read_u16_be(data, off + 4, size);
            t.gsub_feature_list_off = off + read_u16_be(data, off + 6, size);
            t.gsub_lookup_list_off = off + read_u16_be(data, off + 8, size);
        }

        // Parse GPOS
        if (t.gpos_off && t.gpos_len >= 8) {
            u32 off = t.gpos_off;
            t.gpos_script_list_off = off + read_u16_be(data, off + 4, size);
            t.gpos_feature_list_off = off + read_u16_be(data, off + 6, size);
            t.gpos_lookup_list_off = off + read_u16_be(data, off + 8, size);
        }

        // Parse GDEF to get GlyphClassDef
        if (t.gdef_off && t.gdef_len >= 12) {
            u32 off = t.gdef_off;
            u32 class_def_off = read_u16_be(data, off + 12, size);
            if (class_def_off)
                t.gdef_glyph_class_off = off + class_def_off;
        }

        // Find the 'arab' script's lang sys in GSUB
        if (t.gsub_script_list_off) {
            u32 script_off = find_script(data, size, t.gsub_script_list_off, "arab");
            if (!script_off) {
                // Try default ('DFLT')
                script_off = find_script(data, size, t.gsub_script_list_off, "DFLT");
            }
            if (script_off) {
                u32 lang_sys = find_lang_sys(data, size, script_off);
                if (lang_sys) {
                    collect_features(data, size, lang_sys, t.gsub_feature_list_off, t);
                }
            }
        }

        // Also collect features for 'latn' script
        if (t.gsub_script_list_off) {
            u32 script_off = find_script(data, size, t.gsub_script_list_off, "latn");
            if (script_off) {
                u32 lang_sys = find_lang_sys(data, size, script_off);
                if (!lang_sys) {
                    // Try the script's default
                    u32 def_lang = read_u16_be(data, script_off, size);
                    if (def_lang)
                        lang_sys = script_off + def_lang;
                }
                if (lang_sys) {
                    collect_features(data, size, lang_sys, t.gsub_feature_list_off, t);
                }
            }
        }

        // Build the lookup index → offset mapping
        auto build_lookups = [&](u32 list_off, u32 table_off) {
            if (!list_off)
                return;
            u32 count = read_u16_be(data, list_off, size);
            for (u32 i = 0; i < count && i < 100; i++) {
                u32 off_val = read_u16_be(data, list_off + 2 + i * 2, size);
                if (!off_val)
                    continue;
                u32 loff = list_off + off_val;
                if (loff + 6 > size)
                    continue;
                u16 ltype = read_u16_be(data, loff, size);
                u16 lflag = read_u16_be(data, loff + 2, size);
                t.lookups.push_back({loff, ltype, lflag, table_off});
            }
        };

        build_lookups(t.gsub_lookup_list_off, t.gsub_off);
        build_lookups(t.gpos_lookup_list_off, t.gpos_off);
    }

    u32 Shaper::find_script(const u8 *data, u32 size, u32 list_off, const char tag[4]) {
        if (list_off + 2 > size)
            return 0;
        u32 count = read_u16_be(data, list_off, size);
        u32 target = pack_tag(tag);
        for (u32 i = 0; i < count; i++) {
            u32 rec_off = list_off + 2 + i * 6;
            if (rec_off + 6 > size)
                break;
            u32 stag = read_u32_be(data, rec_off, size);
            if (stag == target) {
                u32 soff = read_u16_be(data, rec_off + 4, size);
                if (soff)
                    return list_off + soff;
            }
        }
        return 0;
    }

    u32 Shaper::find_lang_sys(const u8 *data, u32 size, u32 script_off) {
        // Script: DefaultLangSys(u16), LangSysCount(u16), LangSysRecord[]
        // Return offset of the DefaultLangSys table
        if (script_off + 2 > size)
            return 0;
        u32 def_off = read_u16_be(data, script_off, size);
        if (def_off)
            return script_off + def_off;
        // Try first language
        u16 lang_count = read_u16_be(data, script_off + 2, size);
        if (lang_count > 0) {
            u32 lang_rec_off = script_off + 4;
            u32 lang_sys_off = read_u16_be(data, lang_rec_off + 4, size);
            if (lang_sys_off)
                return script_off + lang_sys_off;
        }
        return 0;
    }

    void Shaper::collect_features(const u8 *data, u32 size, u32 lang_sys_off, u32 feature_list_off, ShapingTables &t) {
        if (!feature_list_off || lang_sys_off + 4 > size)
            return;

        u16 feature_count = read_u16_be(data, lang_sys_off + 2, size);
        u32 features_base = lang_sys_off + 4;
        for (u16 i = 0; i < feature_count; i++) {
            if (features_base + i * 2 > size)
                break;
            u16 feature_idx = read_u16_be(data, features_base + i * 2, size);

            // Look up feature record
            u32 feat_rec = feature_list_off + 2 + feature_idx * 6;
            if (feat_rec + 6 > size)
                continue;
            u32 tag = read_u32_be(data, feat_rec, size);
            u32 feat_off = read_u16_be(data, feat_rec + 4, size);
            if (!feat_off)
                continue;

            u32 feat_data = feature_list_off + feat_off;
            if (feat_data + 4 > size)
                continue;
            u16 lookup_count = read_u16_be(data, feat_data + 2, size);
            for (u16 j = 0; j < lookup_count; j++) {
                if (feat_data + 4 + j * 2 > size)
                    break;
                u16 lookup_idx = read_u16_be(data, feat_data + 4 + j * 2, size);
                t.feature_lookups[tag].push_back(lookup_idx);
            }
        }
    }

    // ── Coverage Table ───────────────────────────────────────────────────────

    u16 Shaper::coverage_index(const u8 *data, u32 size, u32 coverage_off, u16 gid) {
        if (coverage_off + 2 > size)
            return 0xFFFF;
        u16 fmt = read_u16_be(data, coverage_off, size);
        if (fmt == 1) {
            u16 glyph_count = read_u16_be(data, coverage_off + 2, size);
            for (u16 i = 0; i < glyph_count; i++) {
                if (coverage_off + 4 + i * 2 > size)
                    break;
                if (read_u16_be(data, coverage_off + 4 + i * 2, size) == gid)
                    return i;
            }
        } else if (fmt == 2) {
            u16 range_count = read_u16_be(data, coverage_off + 2, size);
            for (u16 i = 0; i < range_count; i++) {
                u32 rc_off = coverage_off + 4 + i * 6;
                if (rc_off + 6 > size)
                    break;
                u16 start = read_u16_be(data, rc_off, size);
                u16 end = read_u16_be(data, rc_off + 2, size);
                u16 start_index = read_u16_be(data, rc_off + 4, size);
                if (gid >= start && gid <= end)
                    return (u16)(start_index + (gid - start));
            }
        }
        return 0xFFFF;
    }

    // ── GSUB Lookup Application ──────────────────────────────────────────────

    bool Shaper::apply_lookup(
        ShapingTables &t, const u8 *data, u32 size, u32 lookup_index, u16 *glyphs, u32 *codepoints, u32 &count) {
        if (lookup_index >= t.lookups.size())
            return false;
        auto &lk = t.lookups[lookup_index];
        switch (lk.type) {
            case 1:
                return apply_single(t, data, size, lk.off, lk.flag, glyphs, codepoints, count);
            case 3:
                return apply_alternate(t, data, size, lk.off, lk.flag, glyphs, codepoints, count);
            case 4:
                return apply_ligature(t, data, size, lk.off, lk.flag, glyphs, codepoints, count);
            default:
                return false;
        }
    }

    bool Shaper::apply_single(
        ShapingTables &t, const u8 *data, u32 size, u32 lookup_off, u16 lookup_flag, u16 *glyphs, u32 *, u32 &count) {
        (void)t;
        (void)lookup_flag;
        u16 subtable_count = read_u16_be(data, lookup_off + 4, size);
        for (u16 st = 0; st < subtable_count; st++) {
            u32 st_off_off = lookup_off + 6 + st * 4;
            if (st_off_off + 4 > size)
                break;
            u32 st_off = lookup_off + read_u16_be(data, st_off_off, size);
            if (st_off + 4 > size)
                continue;

            u16 fmt = read_u16_be(data, st_off, size);
            u32 cov_off = st_off + read_u16_be(data, st_off + 2, size);

            for (u32 i = 0; i < count; i++) {
                u16 cidx = coverage_index(data, size, cov_off, glyphs[i]);
                if (cidx == 0xFFFF)
                    continue;

                if (fmt == 1) {
                    // Format 1: DeltaGlyphID
                    i16 delta = (i16)read_i16_be(data, st_off + 4, size);
                    u16 new_gid = (u16)((i32)glyphs[i] + delta);
                    if (new_gid > 0 && new_gid != glyphs[i]) {
                        glyphs[i] = new_gid;
                    }
                    break;  // Only first matching subtable per glyph
                } else if (fmt == 2) {
                    // Format 2: Explicit array of substitute glyph IDs
                    u16 glyph_count = read_u16_be(data, st_off + 4, size);
                    if (cidx < glyph_count) {
                        u16 new_gid = read_u16_be(data, st_off + 6 + cidx * 2, size);
                        if (new_gid > 0 && new_gid != glyphs[i]) {
                            glyphs[i] = new_gid;
                        }
                    }
                    break;
                }
            }
        }
        return true;
    }

    bool Shaper::apply_alternate(
        ShapingTables &t, const u8 *data, u32 size, u32 lookup_off, u16 lookup_flag, u16 *glyphs, u32 *, u32 &count) {
        (void)t;
        (void)lookup_flag;
        u16 subtable_count = read_u16_be(data, lookup_off + 4, size);
        for (u16 st = 0; st < subtable_count; st++) {
            u32 st_off_off = lookup_off + 6 + st * 4;
            if (st_off_off + 4 > size)
                break;
            u32 st_off = lookup_off + read_u16_be(data, st_off_off, size);
            if (st_off + 8 > size)
                continue;

            u32 cov_off = st_off + read_u16_be(data, st_off + 2, size);

            for (u32 i = 0; i < count; i++) {
                u16 cidx = coverage_index(data, size, cov_off, glyphs[i]);
                if (cidx == 0xFFFF)
                    continue;

                u16 glyph_count = read_u16_be(data, st_off + 4, size);
                if (cidx >= glyph_count)
                    break;

                u32 alt_set_off = st_off + read_u16_be(data, st_off + 6 + cidx * 2, size);
                if (alt_set_off + 2 > size)
                    break;
                u16 alt_count = read_u16_be(data, alt_set_off, size);
                if (alt_count > 0) {
                    // Use first alternate (alternate index 0)
                    u16 alt_gid = read_u16_be(data, alt_set_off + 2, size);
                    if (alt_gid > 0 && alt_gid != glyphs[i]) {
                        glyphs[i] = alt_gid;
                    }
                }
                break;  // First match per glyph
            }
        }
        return true;
    }

    bool Shaper::apply_ligature(ShapingTables &t,
                                const u8 *data,
                                u32 size,
                                u32 lookup_off,
                                u16 lookup_flag,
                                u16 *glyphs,
                                u32 *codepoints,
                                u32 &count) {
        (void)t;
        (void)lookup_flag;
        u16 subtable_count = read_u16_be(data, lookup_off + 4, size);
        for (u16 st = 0; st < subtable_count; st++) {
            u32 st_off_off = lookup_off + 6 + st * 4;
            if (st_off_off + 4 > size)
                break;
            u32 st_off = lookup_off + read_u16_be(data, st_off_off, size);
            if (st_off + 6 > size)
                continue;

            u32 cov_off = st_off + read_u16_be(data, st_off + 2, size);

            for (u32 i = 0; i < count; i++) {
                u16 cidx = coverage_index(data, size, cov_off, glyphs[i]);
                if (cidx == 0xFFFF)
                    continue;

                u16 lig_set_count = read_u16_be(data, st_off + 4, size);
                if (cidx >= lig_set_count)
                    continue;

                u32 lig_set_off = st_off + read_u16_be(data, st_off + 6 + cidx * 2, size);
                if (lig_set_off + 2 > size)
                    break;
                u16 lig_count = read_u16_be(data, lig_set_off, size);

                for (u16 li = 0; li < lig_count; li++) {
                    u32 lig_off = lig_set_off + read_u16_be(data, lig_set_off + 2 + li * 2, size);
                    if (lig_off + 6 > size)
                        break;

                    u16 lig_gid = read_u16_be(data, lig_off, size);
                    u16 comp_count = read_u16_be(data, lig_off + 2, size);
                    if (comp_count < 2 || comp_count > 4)
                        continue;

                    // Check if following glyphs match
                    bool match = true;
                    for (u16 c = 1; c < comp_count; c++) {
                        u32 comp_gid_off = lig_off + 4 + (c - 1) * 2;
                        if (comp_gid_off + 2 > size) {
                            match = false;
                            break;
                        }
                        u16 expected = read_u16_be(data, comp_gid_off, size);
                        if (i + c >= count || glyphs[i + c] != expected) {
                            match = false;
                            break;
                        }
                    }

                    if (match) {
                        // Replace first glyph with ligature, collapse following
                        glyphs[i] = lig_gid;
                        // Keep the codepoint of the first (for font fallback purposes)
                        u32 remaining = count - (i + comp_count);
                        if (remaining > 0) {
                            std::memmove(glyphs + i + 1, glyphs + i + comp_count, remaining * sizeof(u16));
                            std::memmove(codepoints + i + 1, codepoints + i + comp_count, remaining * sizeof(u32));
                        }
                        count -= comp_count - 1;
                        break;  // Found longest match for this start
                    }
                }
            }
        }
        return true;
    }

    void Shaper::apply_features(ShapingTables &t,
                                const u8 *data,
                                u32 size,
                                const std::vector<u32> &feature_tags,
                                u16 *glyphs,
                                u32 *codepoints,
                                u32 &count) {
        for (u32 tag : feature_tags) {
            auto it = t.feature_lookups.find(tag);
            if (it == t.feature_lookups.end())
                continue;
            for (u32 lk_idx : it->second) {
                apply_lookup(t, data, size, lk_idx, glyphs, codepoints, count);
            }
        }
    }

    // ── GPOS Pair Positioning ────────────────────────────────────────────────

    static i16 read_value_record(const u8 *data, u32 size, u32 off, u16 fmt, i32 *x_advance_out) {
        *x_advance_out = 0;
        if (!fmt)
            return 0;
        u32 pos = off;
        if (fmt & 0x0001) {  // XPlacement
            pos += 2;
        }
        if (fmt & 0x0002) {  // YPlacement
            pos += 2;
        }
        if (fmt & 0x0004) {  // XAdvance
            if (pos + 2 > size)
                return (i16)pos;
            *x_advance_out = read_i16_be(data, pos, size);
            pos += 2;
        }
        if (fmt & 0x0008) {  // YAdvance
            pos += 2;
        }
        return 0;
    }

    void Shaper::apply_gpos_pair(const u8 *data, u32 size, ShapingTables &t, u16 *glyphs, i32 *advances, u32 count) {
        if (!t.gpos_lookup_list_off || count < 2)
            return;

        u32 list_off = t.gpos_lookup_list_off;
        u32 lookup_count = read_u16_be(data, list_off, size);

        for (u32 li = 0; li < lookup_count; li++) {
            u32 loff = list_off + 2 + li * 2 + read_u16_be(data, list_off + 2 + li * 2, size);
            if (loff + 4 > size)
                continue;
            u16 ltype = read_u16_be(data, loff, size);
            if (ltype != 2)
                continue;  // Only PairPos

            u16 lflag = read_u16_be(data, loff + 2, size);
            (void)lflag;
            u16 subtable_count = read_u16_be(data, loff + 4, size);

            for (u16 st = 0; st < subtable_count; st++) {
                u32 st_off_off = loff + 6 + st * 4;
                if (st_off_off + 4 > size)
                    break;
                u32 st_off = loff + read_u16_be(data, st_off_off, size);
                if (st_off + 8 > size)
                    continue;

                u16 fmt = read_u16_be(data, st_off, size);
                u32 cov_off = st_off + read_u16_be(data, st_off + 2, size);
                u16 val_fmt1 = read_u16_be(data, st_off + 4, size);
                u16 val_fmt2 = read_u16_be(data, st_off + 6, size);

                if (fmt == 1) {
                    // Format 1: Individual pair adjustments
                    u16 pair_set_count = read_u16_be(data, st_off + 8, size);
                    for (u32 idx = 0; idx + 1 < count; idx++) {
                        u16 cidx = coverage_index(data, size, cov_off, glyphs[idx]);
                        if (cidx == 0xFFFF || cidx >= pair_set_count)
                            continue;

                        u32 ps_off = st_off + read_u16_be(data, st_off + 10 + cidx * 2, size);
                        if (ps_off + 2 > size)
                            continue;
                        u16 pair_count = read_u16_be(data, ps_off, size);

                        for (u16 p = 0; p < pair_count; p++) {
                            u32 pr_off = ps_off + 2 + p * (2 + 2 * ((val_fmt1 ? 1 : 0) + (val_fmt2 ? 1 : 0)));
                            if (pr_off + 2 > size)
                                break;
                            u16 second_gid = read_u16_be(data, pr_off, size);
                            if (second_gid != glyphs[idx + 1])
                                continue;

                            i32 x_adv;
                            read_value_record(data, size, pr_off + 2, val_fmt1, &x_adv);
                            advances[idx] += x_adv;
                            break;
                        }
                    }
                } else if (fmt == 2) {
                    // Format 2: Class-based pair adjustments
                    u32 class_def1_off = st_off + read_u16_be(data, st_off + 8, size);
                    u32 class_def2_off = st_off + read_u16_be(data, st_off + 10, size);
                    u16 class1_count = read_u16_be(data, st_off + 12, size);
                    u16 class2_count = read_u16_be(data, st_off + 14, size);

                    if (class1_count == 0 || class2_count == 0)
                        continue;
                    u32 class1_rec_off = st_off + 16;

                    for (u32 idx = 0; idx + 1 < count; idx++) {
                        // Find class for glyph[idx] using class_def1
                        u16 c1 = 0;
                        {
                            u32 def_off = class_def1_off;
                            u16 cfmt = read_u16_be(data, def_off, size);
                            if (cfmt == 1) {
                                u16 glyph_count = read_u16_be(data, def_off + 2, size);
                                for (u16 ci = 0; ci < glyph_count; ci++) {
                                    if (read_u16_be(data, def_off + 4 + ci * 2, size) == glyphs[idx]) {
                                        c1 = ci;
                                        break;
                                    }
                                }
                            } else if (cfmt == 2) {
                                u16 range_count = read_u16_be(data, def_off + 2, size);
                                for (u16 ci = 0; ci < range_count; ci++) {
                                    u32 rc_off = def_off + 4 + ci * 6;
                                    u16 start = read_u16_be(data, rc_off, size);
                                    u16 end = read_u16_be(data, rc_off + 2, size);
                                    u16 cls = read_u16_be(data, rc_off + 4, size);
                                    if (glyphs[idx] >= start && glyphs[idx] <= end) {
                                        c1 = cls;
                                        break;
                                    }
                                }
                            }
                        }

                        // Find class for glyph[idx+1] using class_def2
                        u16 c2 = 0;
                        {
                            u32 def_off = class_def2_off;
                            u16 cfmt = read_u16_be(data, def_off, size);
                            if (cfmt == 1) {
                                u16 glyph_count = read_u16_be(data, def_off + 2, size);
                                for (u16 ci = 0; ci < glyph_count; ci++) {
                                    if (read_u16_be(data, def_off + 4 + ci * 2, size) == glyphs[idx + 1]) {
                                        c2 = ci;
                                        break;
                                    }
                                }
                            } else if (cfmt == 2) {
                                u16 range_count = read_u16_be(data, def_off + 2, size);
                                for (u16 ci = 0; ci < range_count; ci++) {
                                    u32 rc_off = def_off + 4 + ci * 6;
                                    u16 start = read_u16_be(data, rc_off, size);
                                    u16 end = read_u16_be(data, rc_off + 2, size);
                                    u16 cls = read_u16_be(data, rc_off + 4, size);
                                    if (glyphs[idx + 1] >= start && glyphs[idx + 1] <= end) {
                                        c2 = cls;
                                        break;
                                    }
                                }
                            }
                        }

                        if (c1 >= class1_count || c2 >= class2_count)
                            continue;

                        u32 c1_off =
                            class1_rec_off + c1 * class2_count * (2 + 2 * ((val_fmt1 ? 1 : 0) + (val_fmt2 ? 1 : 0)));
                        u32 c2_off = c1_off + c2 * (2 + 2 * ((val_fmt1 ? 1 : 0) + (val_fmt2 ? 1 : 0)));

                        if (c2_off + 2 > size)
                            continue;

                        i32 x_adv;
                        read_value_record(data, size, c2_off + 2, val_fmt1, &x_adv);
                        advances[idx] += x_adv;
                    }
                }
            }
        }
    }

    // ── Arabic shaping ───────────────────────────────────────────────────────

    static bool is_rtl_script(char32_t cp) {
        return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF);
    }

    // ── Main shape() ─────────────────────────────────────────────────────────

    void Shaper::shape(FontFace *face,
                       const std::vector<u32> &codepoints,
                       std::vector<ShapedGlyph> &out,
                       u32 pixel_size) {
        out.clear();
        if (!face || codepoints.empty())
            return;

        const u8 *font_data = face->font_data_ptr();
        u32 font_size = face->font_data_size();

        u32 n = (u32)codepoints.size();
        if (n > 4096)
            n = 4096;

        // Step 1: Map codepoints to glyph IDs via cmap (with font fallback support)
        auto glyphs = std::make_unique<u16[]>(n);
        auto cps = std::make_unique<u32[]>(n);
        for (u32 i = 0; i < n; i++) {
            cps[i] = (u32)codepoints[i];
            glyphs[i] = (u16)face->glyph_index(cps[i]);
        }
        u32 count = n;

        // Step 2: Get or parse font shaping tables
        ShapingTables *tables = get_or_parse(face);

        if (tables && tables->parsed && tables->gsub_len > 0) {
            // Step 3: Detect script
            bool has_arabic = false;
            for (u32 i = 0; i < count; i++) {
                if (is_rtl_script((char32_t)cps[i])) {
                    has_arabic = true;
                    break;
                }
            }

            if (has_arabic) {
                // Arabic shaping: apply features in order:
                // 'init' → 'medi' → 'fina' → 'isol' → 'rlig'

                // Determine joining positions
                // Collect non-transparent character indices
                std::vector<u32> non_transparent;
                non_transparent.reserve(count);
                for (u32 i = 0; i < count; i++) {
                    if (!is_transparent((char32_t)cps[i])) {
                        non_transparent.push_back(i);
                    }
                }

                if (!non_transparent.empty()) {
                    // For each non-transparent character, determine position
                    std::vector<u32> positions(count, 4);  // default: isolated (4)
                    // 0 = initial, 1 = medial, 2 = final, 3 = isolated

                    for (size_t j = 0; j < non_transparent.size(); j++) {
                        u32 i = non_transparent[j];
                        JoiningType jt = get_joining_type((char32_t)cps[i]);

                        // Determine prev and next joining types
                        bool has_prev = (j > 0);
                        bool has_next = (j + 1 < non_transparent.size());
                        JoiningType prev_jt =
                            has_prev ? get_joining_type((char32_t)cps[non_transparent[j - 1]]) : JOIN_U;
                        JoiningType next_jt =
                            has_next ? get_joining_type((char32_t)cps[non_transparent[j + 1]]) : JOIN_U;

                        // Can this character join to the right? (has previous character that should cause it)
                        bool right_joining = has_prev && (prev_jt == JOIN_D || prev_jt == JOIN_R || prev_jt == JOIN_C);
                        // Can this character join to the left? (has next character that accepts it)
                        bool left_joining = has_next && (next_jt == JOIN_D || next_jt == JOIN_L || next_jt == JOIN_C);

                        if (jt == JOIN_D) {
                            if (right_joining && left_joining)
                                positions[i] = 1;  // medial
                            else if (right_joining)
                                positions[i] = 2;  // final
                            else if (left_joining)
                                positions[i] = 0;  // initial
                            else
                                positions[i] = 3;  // isolated
                        } else if (jt == JOIN_R) {
                            if (right_joining)
                                positions[i] = 2;  // final
                            else
                                positions[i] = 3;  // isolated
                        } else if (jt == JOIN_L) {
                            if (left_joining)
                                positions[i] = 0;  // initial
                            else
                                positions[i] = 3;  // isolated
                        }
                    }

                    // Apply GSUB features based on position
                    // Map positions to features
                    // initial → 'init', medial → 'medi', final → 'fina', isolated → 'isol'
                    // We need to apply in order, but each character should only get one substitution
                    for (u32 p = 0; p < 4; p++) {
                        u32 feat_tag;
                        if (p == 0)
                            feat_tag = pack_tag("init");
                        else if (p == 1)
                            feat_tag = pack_tag("medi");
                        else if (p == 2)
                            feat_tag = pack_tag("fina");
                        else
                            feat_tag = pack_tag("isol");

                        auto it = tables->feature_lookups.find(feat_tag);
                        if (it == tables->feature_lookups.end())
                            continue;

                        for (u32 lk_idx : it->second) {
                            // Apply only to glyphs whose position matches
                            // We can't easily apply per-glyph, so we apply to all and trust the font's
                            // coverage table to only match the appropriate glyphs
                            apply_lookup(*tables, font_data, font_size, lk_idx, glyphs.get(), cps.get(), count);
                        }
                    }

                    // Apply 'rlig' (required ligatures)
                    auto rlig_it = tables->feature_lookups.find(pack_tag("rlig"));
                    if (rlig_it != tables->feature_lookups.end()) {
                        for (u32 lk_idx : rlig_it->second) {
                            apply_lookup(*tables, font_data, font_size, lk_idx, glyphs.get(), cps.get(), count);
                        }
                    }
                }
            } else {
                // Non-Arabic scripts: apply 'rlig', 'liga' for ligatures
                u32 liga_tags[] = {pack_tag("rlig"), pack_tag("liga")};
                for (u32 tag : liga_tags) {
                    apply_features(*tables, font_data, font_size, {tag}, glyphs.get(), cps.get(), count);
                }
            }
        }

        // Step 4: Get per-glyph advances and apply GPOS kerning
        auto advances = std::make_unique<i32[]>(count);
        for (u32 i = 0; i < count; i++) {
            // Use hmtx for advance width (from glyph id)
            if (glyphs[i] > 0) {
                auto mr = face->get_metrics_by_gid(glyphs[i], pixel_size);
                if (mr.is_ok()) {
                    advances[i] = mr.unwrap().advance_x;
                } else {
                    advances[i] = (i32)((f32)pixel_size * 0.5f);
                }
            } else {
                // Fallback for missing glyph
                advances[i] = (i32)((f32)pixel_size * 0.5f);
            }
        }

        // Apply GPOS kerning
        if (tables && tables->parsed && tables->gpos_len > 0) {
            // Collect 'kern' feature lookups
            auto kern_it = tables->feature_lookups.find(pack_tag("kern"));
            if (kern_it != tables->feature_lookups.end()) {
                apply_gpos_pair(font_data, font_size, *tables, glyphs.get(), advances.get(), count);
            }
        }

        // Also apply 'kern' table kerning (legacy TrueType)
        if (count >= 2) {
            for (u32 i = 0; i + 1 < count; i++) {
                if (glyphs[i] > 0 && glyphs[i + 1] > 0) {
                    i32 kern = face->get_kerning(glyphs[i], glyphs[i + 1]);
                    if (kern != 0) {
                        f32 scale = (f32)pixel_size / face->units_per_em();
                        advances[i] += (i32)((f32)kern * scale);
                    }
                }
            }
        }

        // Step 5: Build output ShapedGlyphs
        f32 advance_sum = 0;
        for (u32 i = 0; i < count; i++) {
            ShapedGlyph sg;
            sg.glyph_id = glyphs[i];
            sg.codepoint = cps[i];
            sg.face = face;
            sg.advance_x = advances[i];
            sg.x_offset = 0;
            sg.y_offset = 0;
            out.push_back(sg);
            advance_sum += (f32)advances[i];
        }
    }

}  // namespace browser::render
