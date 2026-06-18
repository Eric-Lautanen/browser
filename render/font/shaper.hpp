#pragma once
#include "../../tests/utility.hpp"
#include "font.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::render {

    struct ShapedGlyph {
        u16 glyph_id;
        i32 advance_x;
        i32 x_offset;
        i32 y_offset;
        FontFace *face;
        u32 codepoint;
    };

    class Shaper {
    public:
        Shaper();

        void shape(FontFace *face, const std::vector<u32> &codepoints, std::vector<ShapedGlyph> &out, u32 pixel_size);

    private:
        enum JoiningType : u8 { JOIN_R, JOIN_L, JOIN_D, JOIN_U, JOIN_T, JOIN_C };

        static JoiningType get_joining_type(char32_t cp);
        static bool is_arabic_script(char32_t cp);
        static bool is_transparent(char32_t cp);

    struct ParsedLookup {
        u32 off;       // absolute offset to lookup table
        u16 type;
        u16 flag;
        u32 table_off; // absolute offset to containing GSUB or GPOS table start
    };

        struct ShapingTables {
            u32 gsub_off = 0;
            u32 gsub_len = 0;
            u32 gsub_script_list_off = 0;
            u32 gsub_feature_list_off = 0;
            u32 gsub_lookup_list_off = 0;
            u32 gpos_off = 0;
            u32 gpos_len = 0;
            u32 gpos_script_list_off = 0;
            u32 gpos_feature_list_off = 0;
            u32 gpos_lookup_list_off = 0;
            u32 gdef_off = 0;
            u32 gdef_len = 0;
            u32 gdef_glyph_class_off = 0;

            std::unordered_map<u32, std::vector<u32>> feature_lookups;
            std::vector<ParsedLookup> lookups;
            bool parsed = false;
        };

        ShapingTables *get_or_parse(FontFace *face);

        void parse_gsub_gpos(ShapingTables &t, const u8 *data, u32 size);
        u32 find_script(const u8 *data, u32 size, u32 list_off, const char tag[4]);
        u32 find_lang_sys(const u8 *data, u32 size, u32 script_off);
        void collect_features(const u8 *data, u32 size, u32 lang_sys_off, u32 feature_list_off, ShapingTables &t);

        void apply_features(ShapingTables &t,
                            const u8 *data,
                            u32 size,
                            const std::vector<u32> &feature_tags,
                            u16 *glyphs,
                            u32 *codepoints,
                            u32 &count);

        bool apply_lookup(
            ShapingTables &t, const u8 *data, u32 size, u32 lookup_index, u16 *glyphs, u32 *codepoints, u32 &count);

        bool apply_single(ShapingTables &t,
                          const u8 *data,
                          u32 size,
                          u32 lookup_off,
                          u16 lookup_flag,
                          u16 *glyphs,
                          u32 *codepoints,
                          u32 &count);

        bool apply_ligature(ShapingTables &t,
                            const u8 *data,
                            u32 size,
                            u32 lookup_off,
                            u16 lookup_flag,
                            u16 *glyphs,
                            u32 *codepoints,
                            u32 &count);

        bool apply_alternate(ShapingTables &t,
                             const u8 *data,
                             u32 size,
                             u32 lookup_off,
                             u16 lookup_flag,
                             u16 *glyphs,
                             u32 *codepoints,
                             u32 &count);

        u16 coverage_index(const u8 *data, u32 size, u32 coverage_off, u16 gid);

        void apply_gpos_pair(const u8 *data, u32 size, ShapingTables &t, u16 *glyphs, i32 *advances, u32 count);

        struct JoiningRange {
            u32 lo, hi;
            JoiningType jt;
        };
        static const JoiningRange joining_ranges[];
        static const u32 joining_range_count;

        std::unordered_map<u32, ShapingTables> cache_;
    };

}  // namespace browser::render
