#include "Common.hpp"
#include "GetOpt.hpp"
#include "StringUtil.hpp"
#include "ZCMGen.hpp"

#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

#define FLAG_NONE 0

// flags for emit_c_array_loops_start
#define FLAG_EMIT_MALLOCS 1

// flags for emit_c_array_loops_end
#define FLAG_EMIT_FREES   2

static string dotsToUnderscores(const string& s)
{
    string ret = s;
    for (uint i = 0; i < ret.size(); i++)
        if (ret[i] == '.')
             ret[i] = '_';
    return ret;
}

// Create an accessor for member lm, whose name is "n". For arrays,
// the dim'th dimension is accessed. E.g., dim=0 will have no
// additional brackets, dim=1 has [a], dim=2 has [a][b].
static string makeAccessor(ZCMMember& lm, const string& n, uint dim)
{
    if (lm.dimensions.size() == 0) {
        return "&("+n+"[element]."+lm.membername+")";
    } else {
        string s = n+"[element]."+lm.membername;
        for (uint d = 0; d < dim; d++)
            s += "[" + string(1, d+'a') + "]";
        return s;
    }
}

static string makeArraySize(ZCMMember& lm, const string& n, int dim)
{
    if (lm.dimensions.size() == 0) {
        return "1";
    } else {
        auto& ld = lm.dimensions[dim];
        switch (ld.mode) {
            case ZCM_CONST: return ld.size;
            case ZCM_VAR:   return n+"[element]."+ld.size;
        }
    }
    assert(0 && "Should be unreachable");
}

// Some types do not have a 1:1 mapping from zcm types to native C storage types.
static string mapTypeName(const string& t)
{
    if (t == "boolean") return "int8_t";
    if (t == "string")  return "char*";
    if (t == "byte")    return "uint8_t";

    return dotsToUnderscores(t);
}

struct Emit
{
    ZCMGen& zcm;
    FILE *f = nullptr;

    Emit(ZCMGen& zcm, const string& fname) : zcm(zcm)
    {
        f = fopen(fname.c_str(), "w");
    }

    ~Emit()
    {
        if (f) fclose(f);
    }

    bool good()
    {
        return f != nullptr;
    }

    void emitAutoGeneratedWarning()
    {
        fprintf(f,
                "// THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY\n"
                "// BY HAND!!\n"
                "//\n"
                "// Generated by zcm-gen\n\n");
    }

    /** Emit output that is common to every header file **/
    void emitHeaderTop(const string& name)
    {
        emitAutoGeneratedWarning();

        fprintf(f, "#include <stdint.h>\n");
        fprintf(f, "#include <stdlib.h>\n");
        fprintf(f, "#include <zcm/zcm_coretypes.h>\n");

        if(!zcm.gopt->getBool("c-no-pubsub")) {
            fprintf(f, "#include <zcm/zcm.h>\n");
        }
        fprintf(f, "\n");

        fprintf(f, "#ifndef _%s_h\n", name.c_str());
        fprintf(f, "#define _%s_h\n", name.c_str());
        fprintf(f, "\n");

        fprintf(f, "#ifdef __cplusplus\n");
        fprintf(f, "extern \"C\" {\n");
        fprintf(f, "#endif\n");
        fprintf(f, "\n");

    }

    /** Emit output that is common to every header file **/
    void emitHeaderBottom()
    {
        fprintf(f, "#ifdef __cplusplus\n");
        fprintf(f, "}\n");
        fprintf(f, "#endif\n");
        fprintf(f, "\n");
        fprintf(f, "#endif\n");
    }

    void emitComment(int indent, const string& comment)
    {
        if (comment == "")
            return;

        auto lines = StringUtil::split(comment, '\n');
        if (lines.size() == 1) {
            emit(indent, "/// %s", lines[0].c_str());
        } else {
            emit(indent, "/**");
            for (auto& line : lines) {
                if (line.size() > 0) {
                    emit(indent, " * %s", line.c_str());
                } else {
                    emit(indent, " *");
                }
            }
            emit(indent, " */");
        }
    }

    /** Emit header file output specific to a particular type of struct. **/
    void emitHeaderStruct(ZCMStruct& ls)
    {
        string tn = dotsToUnderscores(ls.structname.fullname);
        string tnUpper = StringUtil::toUpper(tn);

        // include header files required by members
        for (auto& lm : ls.members) {
            if (!ZCMGen::isPrimitiveType(lm.type.fullname) &&
                lm.type.fullname != ls.structname.fullname) {
                string otherTn = dotsToUnderscores(lm.type.fullname);
                fprintf(f, "#include \"%s%s%s.h\"\n",
                        zcm.gopt->getString("cinclude").c_str(),
                        zcm.gopt->getString("cinclude").size()>0 ? "/" : "",
                        otherTn.c_str());
            }
        }

        // output constants
        for (auto& lc : ls.constants) {
            assert(ZCMGen::isLegalConstType(lc.type));
            string suffix = (lc.type == "int64_t") ? "LL" : "";
            emitComment(0, lc.comment.c_str());
            emit(0, "#define %s_%s %s%s", tnUpper.c_str(),
                 lc.membername.c_str(), lc.valstr.c_str(), suffix.c_str());
        }
        if (ls.constants.size() > 0)
            emit(0, "");

        // define the struct
        emitComment(0, ls.comment.c_str());
        emit(0, "typedef struct _%s %s;", tn.c_str(), tn.c_str());
        emit(0, "struct _%s", tn.c_str());
        emit(0, "{");

        for (auto& lm : ls.members) {
            emitComment(1, lm.comment.c_str());

            int ndim = lm.dimensions.size();
            if (ndim == 0) {
                emit(1, "%-10s %s;", mapTypeName(lm.type.fullname).c_str(), lm.membername.c_str());
            } else {
                if (lm.isConstantSizeArray()) {
                    emit_start(1, "%-10s %s", mapTypeName(lm.type.fullname).c_str(), lm.membername.c_str());
                    for (auto& ld : lm.dimensions) {
                        emit_continue("[%s]", ld.size.c_str());
                    }
                    emit_end(";");
                } else {
                    emit_start(1, "%-10s ", mapTypeName(lm.type.fullname).c_str());
                    for (int d = 0; d < ndim; d++)
                        emit_continue("*");
                    emit_end("%s;", lm.membername.c_str());
                }
            }
        }
        emit(0, "};");
        emit(0, "");
    }

    void emitHeaderPrototypes(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0, "/**");
        emit(0, " * Create a deep copy of a %s.", tn_);
        emit(0, " * When no longer needed, destroy it with %s_destroy()", tn_);
        emit(0, " */");
        emit(0,"%s* %s_copy(const %s* to_copy);", tn_, tn_, tn_);
        emit(0, "");
        emit(0, "/**");
        emit(0, " * Destroy an instance of %s created by %s_copy()", tn_, tn_);
        emit(0, " */");
        emit(0,"void %s_destroy(%s* to_destroy);", tn_, tn_);
        emit(0,"");

        if (!zcm.gopt->getBool("c-no-pubsub")) {
            emit(0, "/**");
            emit(0, " * Identifies a single subscription.  This is an opaque data type.");
            emit(0, " */");
            emit(0,"typedef struct _%s_subscription_t %s_subscription_t;", tn_, tn_);
            emit(0, "");
            emit(0, "/**");
            emit(0, " * Prototype for a callback function invoked when a message of type");
            emit(0, " * %s is received.", tn_);
            emit(0, " */");
            emit(0,"typedef void(*%s_handler_t)(const zcm_recv_buf_t *rbuf,\n"
                 "             const char *channel, const %s *msg, void *userdata);",
                 tn_, tn_);
            emit(0, "");
            emit(0, "/**");
            emit(0, " * Publish a message of type %s using ZCM.", tn_);
            emit(0, " *");
            emit(0, " * @param zcm The ZCM instance to publish with.");
            emit(0, " * @param channel The channel to publish on.");
            emit(0, " * @param msg The message to publish.");
            emit(0, " * @return 0 on success, <0 on error.  Success means ZCM has transferred");
            emit(0, " * responsibility of the message data to the OS.");
            emit(0, " */");
            emit(0,"int %s_publish(zcm_t *zcm, const char *channel, const %s *msg);", tn_, tn_);
            emit(0, "");
            emit(0, "/**");
            emit(0, " * Subscribe to messages of type %s using ZCM.", tn_);
            emit(0, " *");
            emit(0, " * @param zcm The ZCM instance to subscribe with.");
            emit(0, " * @param channel The channel to subscribe to.");
            emit(0, " * @param handler The callback function invoked by ZCM when a message is received.");
            emit(0, " *                This function is invoked by ZCM during calls to zcm_handle() and");
            emit(0, " *                zcm_handle_timeout().");
            emit(0, " * @param userdata An opaque pointer passed to @p handler when it is invoked.");
            emit(0, " * @return 0 on success, <0 if an error occured");
            emit(0, " */");
            emit(0,"%s_subscription_t* %s_subscribe(zcm_t *zcm, const char *channel, %s_handler_t handler, void *userdata);",
                 tn_, tn_, tn_);
            emit(0, "");
            emit(0, "/**");
            emit(0, " * Removes and destroys a subscription created by %s_subscribe()", tn_);
            emit(0, " */");
            emit(0,"int %s_unsubscribe(zcm_t *zcm, %s_subscription_t* hid);", tn_, tn_);
            emit(0, "");
            emit(0, "/**");
            emit(0, " * Sets the queue capacity for a subscription.");
            emit(0, " * Some ZCM providers (e.g., the default multicast provider) are implemented");
            emit(0, " * using a background receive thread that constantly revceives messages from");
            emit(0, " * the network.  As these messages are received, they are buffered on");
            emit(0, " * per-subscription queues until dispatched by zcm_handle().  This function");
            emit(0, " * how many messages are queued before dropping messages.");
            emit(0, " *");
            emit(0, " * @param subs the subscription to modify.");
            emit(0, " * @param num_messages The maximum number of messages to queue");
            emit(0, " *  on the subscription.");
            emit(0, " * @return 0 on success, <0 if an error occured");
            emit(0, " */");
            emit(0,"int %s_subscription_set_queue_capacity(%s_subscription_t* subs,\n"
                 "                              int num_messages);\n", tn_, tn_);
        }

        emit(0, "/**");
        emit(0, " * Encode a message of type %s into binary form.", tn_);
        emit(0, " *");
        emit(0, " * @param buf The output buffer.");
        emit(0, " * @param offset Encoding starts at this byte offset into @p buf.");
        emit(0, " * @param maxlen Maximum number of bytes to write.  This should generally");
        emit(0, " *               be equal to %s_encoded_size().", tn_);
        emit(0, " * @param msg The message to encode.");
        emit(0, " * @return The number of bytes encoded, or <0 if an error occured.");
        emit(0, " */");
        emit(0,"int %s_encode(void *buf, int offset, int maxlen, const %s *p);", tn_, tn_);
        emit(0, "");
        emit(0, "/**");
        emit(0, " * Decode a message of type %s from binary form.", tn_);
        emit(0, " * When decoding messages containing strings or variable-length arrays, this");
        emit(0, " * function may allocate memory.  When finished with the decoded message,");
        emit(0, " * release allocated resources with %s_decode_cleanup().", tn_);
        emit(0, " *");
        emit(0, " * @param buf The buffer containing the encoded message");
        emit(0, " * @param offset The byte offset into @p buf where the encoded message starts.");
        emit(0, " * @param maxlen The maximum number of bytes to read while decoding.");
        emit(0, " * @param msg Output parameter where the decoded message is stored");
        emit(0, " * @return The number of bytes decoded, or <0 if an error occured.");
        emit(0, " */");
        emit(0,"int %s_decode(const void *buf, int offset, int maxlen, %s *msg);", tn_, tn_);
        emit(0, "");
        emit(0, "/**");
        emit(0, " * Release resources allocated by %s_decode()", tn_);
        emit(0, " * @return 0");
        emit(0, " */");
        emit(0,"int %s_decode_cleanup(%s *p);", tn_, tn_);
        emit(0, "");
        emit(0, "/**");
        emit(0, " * Check how many bytes are required to encode a message of type %s", tn_);
        emit(0, " */");
        emit(0,"int %s_encoded_size(const %s *p);", tn_, tn_);
        if(zcm.gopt->getBool("c-typeinfo")) {
            emit(0,"size_t %s_struct_size(void);", tn_);
            emit(0,"int  %s_num_fields(void);", tn_);
            emit(0,"int  %s_get_field(const %s *p, int i, zcm_field_t *f);", tn_, tn_);
            emit(0,"const zcm_type_info_t *%s_get_type_info(void);", tn_);
        }
        emit(0,"");

        emit(0,"// ZCM support functions. Users should not call these");
        emit(0,"int64_t __%s_get_hash(void);", tn_);
        emit(0,"int64_t __%s_hash_recursive(const __zcm_hash_ptr *p);", tn_);
        emit(0,"int     __%s_encode_array(void *buf, int offset, int maxlen, const %s *p, int elements);", tn_, tn_);
        emit(0,"int     __%s_decode_array(const void *buf, int offset, int maxlen, %s *p, int elements);", tn_, tn_);
        emit(0,"int     __%s_decode_array_cleanup(%s *p, int elements);", tn_, tn_);
        emit(0,"int     __%s_encoded_array_size(const %s *p, int elements);", tn_, tn_);
        emit(0,"int     __%s_clone_array(const %s *p, %s *q, int elements);", tn_, tn_, tn_);
        emit(0,"");
    }

    void emitCStructGetHash(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0, "static int __%s_hash_computed;", tn_);
        emit(0, "static int64_t __%s_hash;", tn_);
        emit(0, "");

        emit(0, "int64_t __%s_hash_recursive(const __zcm_hash_ptr *p)", tn_);
        emit(0, "{");
        emit(1,     "const __zcm_hash_ptr *fp;");
        emit(1,     "for (fp = p; fp != NULL; fp = fp->parent)");
        emit(2,         "if (fp->v == __%s_get_hash)", tn_);
        emit(3,              "return 0;");
        emit(0, "");
        emit(1, "__zcm_hash_ptr cp;");
        emit(1, "cp.parent =  p;");
        emit(1, "cp.v = (void*)__%s_get_hash;", tn_);
        emit(1, "(void) cp;");
        emit(0, "");
        emit(1, "int64_t hash = (int64_t)0x%016" PRIx64 "LL", ls.hash);

        for (auto& lm : ls.members)
            emit(2, " + __%s_hash_recursive(&cp)", dotsToUnderscores(lm.type.fullname).c_str());

        emit(2,";");
        emit(0, "");
        emit(1, "return (hash<<1) + ((hash>>63)&1);");
        emit(0, "}");
        emit(0, "");

        emit(0, "int64_t __%s_get_hash(void)", tn_);
        emit(0, "{");
        emit(1, "if (!__%s_hash_computed) {", tn_);
        emit(2,      "__%s_hash = __%s_hash_recursive(NULL);", tn_, tn_);
        emit(2,      "__%s_hash_computed = 1;", tn_);
        emit(1,      "}");
        emit(0, "");
        emit(1, "return __%s_hash;", tn_);
        emit(0, "}");
        emit(0, "");
    }

    void emitCArrayLoopsStart(ZCMMember& lm, const string& n, int flags)
    {
        if (lm.dimensions.size() == 0)
            return;

        for (uint i = 0; i < lm.dimensions.size() - 1; i++) {
            char var = 'a' + i;

            if (flags & FLAG_EMIT_MALLOCS) {
                string stars = string(lm.dimensions.size()-1-i, '*');
                emit(2+i, "%s = (%s%s*) zcm_malloc(sizeof(%s%s) * %s);",
                     makeAccessor(lm, n, i).c_str(),
                     mapTypeName(lm.type.fullname).c_str(),
                     stars.c_str(),
                     mapTypeName(lm.type.fullname).c_str(),
                     stars.c_str(),
                     makeArraySize(lm, n, i).c_str());
            }

            emit(2+i, "{ int %c;", var);
            emit(2+i, "for (%c = 0; %c < %s; %c++) {", var, var, makeArraySize(lm, "p", i).c_str(), var);
        }

        if (flags & FLAG_EMIT_MALLOCS) {
            emit(2 + (int)lm.dimensions.size() - 1, "%s = (%s*) zcm_malloc(sizeof(%s) * %s);",
                 makeAccessor(lm, n, lm.dimensions.size() - 1).c_str(),
                 mapTypeName(lm.type.fullname).c_str(),
                 mapTypeName(lm.type.fullname).c_str(),
                 makeArraySize(lm, n, lm.dimensions.size() - 1).c_str());
        }
    }

    void emitCArrayLoopsEnd(ZCMMember& lm, const string& n, int flags)
    {
        if (lm.dimensions.size() == 0)
            return;

        auto sz = lm.dimensions.size();
        for (uint i = 0; i < sz - 1; i++) {
            uint indent = sz - i;
            if (flags & FLAG_EMIT_FREES) {
                string accessor = makeAccessor(lm, "p", sz-1-i);
                emit(indent+1, "if (%s) free(%s);", accessor.c_str(), accessor.c_str());
            }
            emit(indent, "}");
            emit(indent, "}");
        }

        if (flags & FLAG_EMIT_FREES) {
            string accessor = makeAccessor(lm, "p", 0);
            emit(2, "if (%s) free(%s);", accessor.c_str(), accessor.c_str());
        }
    }

    void emitCEncodeArray(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int __%s_encode_array(void *buf, int offset, int maxlen, const %s *p, int elements)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen, element;");
        emit(0,"");
        emit(1,    "for (element = 0; element < elements; element++) {");
        emit(0,"");
        for (auto& lm : ls.members) {
            emitCArrayLoopsStart(lm, "p", FLAG_NONE);

            int indent = 2+std::max(0, (int)lm.dimensions.size() - 1);
            emit(indent, "thislen = __%s_encode_array(buf, offset + pos, maxlen - pos, %s, %s);",
                 dotsToUnderscores(lm.type.fullname).c_str(),
                 makeAccessor(lm, "p", lm.dimensions.size()-1).c_str(),
                 makeArraySize(lm, "p", lm.dimensions.size()-1).c_str());
            emit(indent, "if (thislen < 0) return thislen; else pos += thislen;");

            emitCArrayLoopsEnd(lm, "p", FLAG_NONE);
            emit(0,"");
        }
        emit(1,   "}");
        emit(1, "return pos;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCEncode(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_encode(void *buf, int offset, int maxlen, const %s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen;");
        emit(1,    "int64_t hash = __%s_get_hash();", tn_);
        emit(0,"");
        emit(1,    "thislen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);");
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0,"");
        emit(1,    "thislen = __%s_encode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0,"");
        emit(1, "return pos;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCDecodeArray(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int __%s_decode_array(const void *buf, int offset, int maxlen, %s *p, int elements)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen, element;");
        emit(0,"");
        emit(1,    "for (element = 0; element < elements; element++) {");
        emit(0,"");
        for (auto& lm : ls.members) {
            emitCArrayLoopsStart(lm, "p", lm.isConstantSizeArray() ? FLAG_NONE : FLAG_EMIT_MALLOCS);

            int indent = 2+std::max(0, (int)lm.dimensions.size() - 1);
            emit(indent, "thislen = __%s_decode_array(buf, offset + pos, maxlen - pos, %s, %s);",
                 dotsToUnderscores(lm.type.fullname).c_str(),
                 makeAccessor(lm, "p", lm.dimensions.size() - 1).c_str(),
                 makeArraySize(lm, "p", lm.dimensions.size() - 1).c_str());
            emit(indent, "if (thislen < 0) return thislen; else pos += thislen;");

            emitCArrayLoopsEnd(lm, "p", FLAG_NONE);
            emit(0,"");
        }
        emit(1,   "}");
        emit(1, "return pos;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCDecodeArrayCleanup(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int __%s_decode_array_cleanup(%s *p, int elements)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int element;");
        emit(1,    "for (element = 0; element < elements; element++) {");
        emit(0,"");
        for (auto& lm : ls.members) {
            emitCArrayLoopsStart(lm, "p", FLAG_NONE);

            int indent = 2+std::max(0, (int)lm.dimensions.size() - 1);
            emit(indent, "__%s_decode_array_cleanup(%s, %s);",
                 dotsToUnderscores(lm.type.fullname).c_str(),
                 makeAccessor(lm, "p", lm.dimensions.size() - 1).c_str(),
                 makeArraySize(lm, "p", lm.dimensions.size() - 1).c_str());

            emitCArrayLoopsEnd(lm, "p", lm.isConstantSizeArray() ? FLAG_NONE : FLAG_EMIT_FREES);
            emit(0,"");
        }
        emit(1,   "}");
        emit(1, "return 0;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCDecode(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_decode(const void *buf, int offset, int maxlen, %s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen;");
        emit(1,    "int64_t hash = __%s_get_hash();", tn_);
        emit(0,"");
        emit(1,    "int64_t this_hash;");
        emit(1,    "thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &this_hash, 1);");
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(1,    "if (this_hash != hash) return -1;");
        emit(0,"");
        emit(1,    "thislen = __%s_decode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0,"");
        emit(1, "return pos;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCDecodeCleanup(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_decode_cleanup(%s *p)", tn_, tn_);
        emit(0,"{");
        emit(1, "return __%s_decode_array_cleanup(p, 1);", tn_);
        emit(0,"}");
        emit(0,"");
    }

    void emitCEncodedArraySize(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int __%s_encoded_array_size(const %s *p, int elements)", tn_, tn_);
        emit(0,"{");
        emit(1,"int size = 0, element;");
        emit(1,    "for (element = 0; element < elements; element++) {");
        emit(0,"");
        for (auto& lm : ls.members) {
            emitCArrayLoopsStart(lm, "p", FLAG_NONE);

            int indent = 2+std::max(0, (int)lm.dimensions.size() - 1);
            emit(indent, "size += __%s_encoded_array_size(%s, %s);",
                 dotsToUnderscores(lm.type.fullname).c_str(),
                 makeAccessor(lm, "p", lm.dimensions.size() - 1).c_str(),
                 makeArraySize(lm, "p", lm.dimensions.size() - 1).c_str());

            emitCArrayLoopsEnd(lm, "p", FLAG_NONE);
            emit(0,"");
        }
        emit(1,"}");
        emit(1, "return size;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCEncodedSize(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_encoded_size(const %s *p)", tn_, tn_);
        emit(0,"{");
        emit(1, "return 8 + __%s_encoded_array_size(p, 1);", tn_);
        emit(0,"}");
        emit(0,"");
    }

    void emitCNumFields(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_num_fields(void)", tn_);
        emit(0,"{");
        emit(1, "return %d;", (int)ls.members.size());
        emit(0,"}");
        emit(0,"");
    }

    void emitCStructSize(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"size_t %s_struct_size(void)", tn_);
        emit(0,"{");
        emit(1, "return sizeof(%s);", tn_);
        emit(0,"}");
        emit(0,"");
    }

    void emitCGetField(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int %s_get_field(const %s *p, int i, zcm_field_t *f)", tn_, tn_);
        emit(0,"{");
        emit(1,"if (0 > i || i >= %s_num_fields())", tn_);
        emit(2,"return 1;");
        emit(1,"");

        emit(1,"switch (i) {");
        emit(1,"");

        int num_fields = ls.members.size();
        for(int i = 0; i < num_fields; i++) {
            emit(2,"case %d: {", i);

            ZCMMember& m = ls.members[i];

            string typeval;
            if(ZCMGen::isPrimitiveType(m.type.shortname)) {
                typeval = "ZCM_FIELD_" + StringUtil::toUpper(m.type.shortname);
            } else {
                emit(3,"/* %s */", m.type.shortname.c_str());
                typeval = "ZCM_FIELD_USER_TYPE";
            }

            emit(3,"f->name = \"%s\";", m.membername.c_str());
            emit(3,"f->type = %s;", typeval.c_str());
            emit(3,"f->typestr = \"%s\";", m.type.shortname.c_str());

            int num_dim = m.dimensions.size();
            emit(3,"f->num_dim = %d;", num_dim);

            if(num_dim != 0) {

                for(int j = 0; j < num_dim; j++) {
                    ZCMDimension& d = m.dimensions[j];
                    if(d.mode == ZCM_VAR)
                        emit(3,"f->dim_size[%d] = p->%s;", j, d.size.c_str());
                    else
                        emit(3,"f->dim_size[%d] = %s;", j, d.size.c_str());
                }

                for(int j = 0; j < num_dim; j++) {
                    ZCMDimension& d = m.dimensions[j];
                    emit(3,"f->dim_is_variable[%d] = %d;", j, d.mode == ZCM_VAR);
                }

            }

            emit(3, "f->data = (void *) &p->%s;", m.membername.c_str());

            emit(3, "return 0;");
            emit(2,"}");
            emit(2,"");
        }
        emit(2,"default:");
        emit(3,"return 1;");
        emit(1,"}");
        emit(0,"}");
        emit(0,"");
    }

    void emitCGetTypeInfo(ZCMStruct& ls)
    {
        string tmp_ = dotsToUnderscores(ls.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"const zcm_type_info_t *%s_get_type_info(void)", tn_);
        emit(0,"{");
        emit(1,"static int init = 0;");
        emit(1,"static zcm_type_info_t typeinfo;");
        emit(1,"if (!init) {");
        emit(2,"typeinfo.encode         = (zcm_encode_t) %s_encode;", tn_);
        emit(2,"typeinfo.decode         = (zcm_decode_t) %s_decode;", tn_);
        emit(2,"typeinfo.decode_cleanup = (zcm_decode_cleanup_t) %s_decode_cleanup;", tn_);
        emit(2,"typeinfo.encoded_size   = (zcm_encoded_size_t) %s_encoded_size;", tn_);
        emit(2,"typeinfo.struct_size    = (zcm_struct_size_t)  %s_struct_size;", tn_);
        emit(2,"typeinfo.num_fields     = (zcm_num_fields_t) %s_num_fields;", tn_);
        emit(2,"typeinfo.get_field      = (zcm_get_field_t) %s_get_field;", tn_);
        emit(2,"typeinfo.get_hash       = (zcm_get_hash_t) __%s_get_hash;", tn_);
        emit(1,"}");
        emit(1,"");
        emit(1,"return &typeinfo;");
        emit(0,"}");
    }

    void emitCCloneArray(ZCMStruct& lr)
    {
        string tmp_ = dotsToUnderscores(lr.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"int __%s_clone_array(const %s *p, %s *q, int elements)", tn_, tn_, tn_);
        emit(0,"{");
        emit(1,    "int element;");
        emit(1,    "for (element = 0; element < elements; element++) {");
        emit(0,"");
        for (auto& lm : lr.members) {

            emitCArrayLoopsStart(lm, "q", lm.isConstantSizeArray() ? FLAG_NONE : FLAG_EMIT_MALLOCS);

            int indent = 2+std::max(0, (int)lm.dimensions.size() - 1);
            emit(indent, "__%s_clone_array(%s, %s, %s);",
                 dotsToUnderscores(lm.type.fullname).c_str(),
                 makeAccessor(lm, "p", lm.dimensions.size()-1).c_str(),
                 makeAccessor(lm, "q", lm.dimensions.size()-1).c_str(),
                 makeArraySize(lm, "p", lm.dimensions.size()-1).c_str());

            emitCArrayLoopsEnd(lm, "p", FLAG_NONE);
            emit(0,"");
        }
        emit(1,   "}");
        emit(1,   "return 0;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCCopy(ZCMStruct& lr)
    {
        string tmp_ = dotsToUnderscores(lr.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"%s *%s_copy(const %s *p)", tn_, tn_, tn_);
        emit(0,"{");
        emit(1,    "%s *q = (%s*) malloc(sizeof(%s));", tn_, tn_, tn_);
        emit(1,    "__%s_clone_array(p, q, 1);", tn_);
        emit(1,    "return q;");
        emit(0,"}");
        emit(0,"");
    }

    void emitCDestroy(ZCMStruct& lr)
    {
        string tmp_ = dotsToUnderscores(lr.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        emit(0,"void %s_destroy(%s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "__%s_decode_array_cleanup(p, 1);", tn_);
        emit(1,    "free(p);");
        emit(0,"}");
        emit(0,"");
    }

    void emitCStructPublish(ZCMStruct& lr)
    {
        string tmp_ = dotsToUnderscores(lr.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();
        fprintf(f,
                "int %s_publish(zcm_t *lc, const char *channel, const %s *p)\n"
                "{\n"
                "      int max_data_size = %s_encoded_size (p);\n"
                "      uint8_t *buf = (uint8_t*) malloc (max_data_size);\n"
                "      if (!buf) return -1;\n"
                "      int data_size = %s_encode (buf, 0, max_data_size, p);\n"
                "      if (data_size < 0) {\n"
                "          free (buf);\n"
                "          return data_size;\n"
                "      }\n"
                "      int status = zcm_publish (lc, channel, buf, data_size);\n"
                "      free (buf);\n"
                "      return status;\n"
                "}\n\n", tn_, tn_, tn_, tn_);
    }


    void emitCStructSubscribe(ZCMStruct& lr)
    {
        string tmp_ = dotsToUnderscores(lr.structname.fullname);
        char *tn_ = (char *)tmp_.c_str();

        fprintf(f,
                "struct _%s_subscription_t {\n"
                "    %s_handler_t user_handler;\n"
                "    void *userdata;\n"
                "    zcm_subscription_t *lc_h;\n"
                "};\n", tn_, tn_);
        fprintf(f,
                "static\n"
                "void %s_handler_stub (const zcm_recv_buf_t *rbuf,\n"
                "                            const char *channel, void *userdata)\n"
                "{\n"
                "    int status;\n"
                "    %s p;\n"
                "    memset(&p, 0, sizeof(%s));\n"
                "    status = %s_decode (rbuf->data, 0, rbuf->data_size, &p);\n"
                "    if (status < 0) {\n"
                "        fprintf (stderr, \"error %%d decoding %s!!!\\n\", status);\n"
                "        return;\n"
                "    }\n"
                "\n"
                "    %s_subscription_t *h = (%s_subscription_t*) userdata;\n"
                "    h->user_handler (rbuf, channel, &p, h->userdata);\n"
                "\n"
                "    %s_decode_cleanup (&p);\n"
                "}\n\n", tn_, tn_, tn_, tn_, tn_, tn_, tn_, tn_
                );

        fprintf(f,
                "%s_subscription_t* %s_subscribe (zcm_t *zcm,\n"
                "                    const char *channel,\n"
                "                    %s_handler_t f, void *userdata)\n"
                "{\n"
                "    %s_subscription_t *n = (%s_subscription_t*)\n"
                "                       malloc(sizeof(%s_subscription_t));\n"
                "    n->user_handler = f;\n"
                "    n->userdata = userdata;\n"
                "    n->lc_h = zcm_subscribe (zcm, channel,\n"
                "                                 %s_handler_stub, n);\n"
                "    if (n->lc_h == NULL) {\n"
                "        fprintf (stderr,\"couldn't reg %s ZCM handler!\\n\");\n"
                "        free (n);\n"
                "        return NULL;\n"
                "    }\n"
                "    return n;\n"
                "}\n\n", tn_, tn_, tn_, tn_, tn_, tn_, tn_, tn_
                );

        fprintf(f,
                "int %s_subscription_set_queue_capacity (%s_subscription_t* subs,\n"
                "                              int num_messages)\n"
                "{\n"
                "    return zcm_subscription_set_queue_capacity (subs->lc_h, num_messages);\n"
                "}\n\n", tn_, tn_);

        fprintf(f,
                "int %s_unsubscribe(zcm_t *zcm, %s_subscription_t* hid)\n"
                "{\n"
                "    int status = zcm_unsubscribe (zcm, hid->lc_h);\n"
                "    if (0 != status) {\n"
                "        fprintf(stderr,\n"
                "           \"couldn't unsubscribe %s_handler %%p!\\n\", hid);\n"
                "        return -1;\n"
                "    }\n"
                "    free (hid);\n"
                "    return 0;\n"
                "}\n\n", tn_, tn_, tn_
                );
    }
};

static int emitStructHeader(ZCMGen& zcm, ZCMStruct& lr, const string& fname)
{
    string tmp_ = dotsToUnderscores(lr.structname.fullname);
    char *tn_ = (char *)tmp_.c_str();

    Emit E{zcm, fname};
    if (!E.good())
        return -1;

    E.emitHeaderTop(tn_);
    E.emitHeaderStruct(lr);
    E.emitHeaderPrototypes(lr);

    E.emitHeaderBottom();
    return 0;
}

static int emitStructSource(ZCMGen& zcm, ZCMStruct& lr, const string& fname)
{
    string tmp_ = dotsToUnderscores(lr.structname.fullname);
    char *tn_ = (char *)tmp_.c_str();

    Emit E{zcm, fname};
    if (!E.good())
        return -1;

    E.emitAutoGeneratedWarning();
    fprintf(E.f, "#include <string.h>\n");
    fprintf(E.f, "#include \"%s%s%s.h\"\n",
            zcm.gopt->getString("cinclude").c_str(),
            zcm.gopt->getString("cinclude").size()>0 ? "/" : "",
            tn_);
    fprintf(E.f, "\n");

    E.emitCStructGetHash(lr);
    E.emitCEncodeArray(lr);
    E.emitCEncode(lr);
    E.emitCEncodedArraySize(lr);
    E.emitCEncodedSize(lr);

    if(zcm.gopt->getBool("c-typeinfo")) {
        E.emitCStructSize(lr);
        E.emitCNumFields(lr);
        E.emitCGetField(lr);
        E.emitCGetTypeInfo(lr);
    }

    E.emitCDecodeArray(lr);
    E.emitCDecodeArrayCleanup(lr);
    E.emitCDecode(lr);
    E.emitCDecodeCleanup(lr);

    E.emitCCloneArray(lr);
    E.emitCCopy(lr);
    E.emitCDestroy(lr);

    if(!zcm.gopt->getBool("c-no-pubsub")) {
        E.emitCStructPublish(lr);
        E.emitCStructSubscribe(lr);
    }

    return 0;
}

void setupOptionsC(GetOpt& gopt)
{
    gopt.addString(0, "c-cpath",    ".",      "Location for .c files");
    gopt.addString(0, "c-hpath",    ".",      "Location for .h files");
    gopt.addString(0, "cinclude",   "",       "Generated #include lines reference this folder");
    gopt.addBool(0, "c-no-pubsub",   0,     "Do not generate _publish and _subscribe functions");
    gopt.addBool(0, "c-typeinfo",   0,      "Generate typeinfo functions for each type");
}

int emitC(ZCMGen& zcm)
{
    for (auto& lr : zcm.structs) {

        string headerName = zcm.gopt->getString("c-hpath") + "/" + lr.nameUnderscore() + ".h";
        string cName      = zcm.gopt->getString("c-cpath") + "/" + lr.nameUnderscore() + ".c";

        // STRUCT H file
        if (zcm.needsGeneration(lr.zcmfile, headerName)) {
            if (int ret = emitStructHeader(zcm, lr, headerName))
                return ret;
        }

        // STRUCT C file
        if (zcm.needsGeneration(lr.zcmfile, cName)) {
            if (int ret = emitStructSource(zcm, lr, cName))
                return ret;
        }
    }

    return 0;
}
