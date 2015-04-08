/**********************************************************\
|                                                          |
|                          hprose                          |
|                                                          |
| Official WebSite: http://www.hprose.com/                 |
|                   http://www.hprose.org/                 |
|                                                          |
\**********************************************************/

/**********************************************************\
 *                                                        *
 * hprose_client.c                                        *
 *                                                        *
 * hprose client for pecl source file.                    *
 *                                                        *
 * LastModified: Apr 8, 2015                              *
 * Author: Ma Bingyao <andot@hprose.com>                  *
 *                                                        *
\**********************************************************/

#include "hprose_tags.h"
#include "hprose_bytes_io.h"
#include "hprose_writer.h"
#include "hprose_reader.h"
#include "hprose_result_mode.h"
#include "hprose_client.h"

static zend_always_inline void hprose_client_do_output(hprose_client *_this, zval *name, zval *args, zend_bool byref, zend_bool simple, zval *context, zval *return_value TSRMLS_DC) {
    hprose_bytes_io stream;
    hprose_writer writer;
    HashTable *ht;
    int32_t i;
    hprose_bytes_io_init(&stream, NULL, 0);
    hprose_bytes_io_putc(&stream, HPROSE_TAG_CALL);
    hprose_writer_init(&writer, &stream, simple);
    hprose_writer_write_string(&writer, name);
    if (Z_ARRLEN_P(args) > 0 || byref) {
        hprose_writer_reset(&writer);
        hprose_writer_write_array(&writer, args TSRMLS_CC);
        if (byref) {
            hprose_writer_write_true(&writer);
        }
    }
    hprose_writer_destroy(&writer);
    hprose_bytes_io_putc(&stream, HPROSE_TAG_END);
#if PHP_MAJOR_VERSION < 7
    RETVAL_STRINGL_0(HB_BUF(stream), HB_LEN(stream));
#else
    RETVAL_STR(HB_STR(stream));
#endif
    ht = Z_ARRVAL_P(_this->filters);
    i = zend_hash_num_elements(ht);
    if (i) {
        zend_hash_internal_pointer_reset(ht);
        for (; i > 0; --i) {
#if PHP_MAJOR_VERSION < 7
            zval **filter;
            zend_hash_get_current_data(ht, (void **)&filter);
            method_invoke_ex(*filter, outputFilter, return_value, 1, "zz", return_value, context);
#else
            zval *filter = zend_hash_get_current_data(ht);
            method_invoke_ex(filter, outputFilter, return_value, 1, "zz", return_value, context);
#endif
            if (EG(exception)) return;
            zend_hash_move_forward(ht);
        }
    }
}

static zend_always_inline void hprose_client_do_input(hprose_client *_this, zval *response, zval *args, int mode, zval *context, zval *return_value TSRMLS_DC) {
    HashTable *ht = Z_ARRVAL_P(_this->filters);
    int32_t i = zend_hash_num_elements(ht);
    if (i) {
        zend_hash_internal_pointer_end(ht);
        for (; i > 0; --i) {
#if PHP_MAJOR_VERSION < 7
            zval **filter;
            zend_hash_get_current_data(ht, (void **)&filter);
            method_invoke_ex(*filter, inputFilter, response, 1, "zz", response, context);
#else
            zval *filter = zend_hash_get_current_data(ht);
            method_invoke_ex(filter, inputFilter, response, 1, "zz", response, context);
#endif
            if (EG(exception)) return;
            zend_hash_move_backwards(ht);
        }
    }
    if (Z_STRLEN_P(response) == 0) {
        zend_throw_exception(NULL, "No Response", 0 TSRMLS_CC);
        return;
    }
    if (mode == HPROSE_RESULT_MODE_RAW_WITH_END_TAG) {
        RETVAL_ZVAL(response, 0, 0);
        ZVAL_NULL(response);
        return;
    }
    else if (mode == HPROSE_RESULT_MODE_RAW) {
#if PHP_MAJOR_VERSION < 7
        RETVAL_STRINGL_0(Z_STRVAL_P(response), Z_STRLEN_P(response) - 1);
#else
        RETVAL_STR(Z_STR_P(response));
        Z_STRLEN_P(return_value) = Z_STRLEN_P(return_value) - 1;
#endif
        Z_STRVAL_P(return_value)[Z_STRLEN_P(return_value)] = '\0';
        ZVAL_NULL(response);        
        return;
    }
    else {
        hprose_bytes_io stream;
        hprose_reader reader;
#if PHP_MAJOR_VERSION < 7
        hprose_bytes_io_init_readonly(&stream, Z_STRVAL_P(response), Z_STRLEN_P(response));
#else
        hprose_bytes_io_init_readonly(&stream, Z_STR_P(response));
#endif
        hprose_reader_init(&reader, &stream, 0);
        char tag;
        RETVAL_NULL();
        while ((tag = hprose_bytes_io_getc(&stream)) != HPROSE_TAG_END) {
            switch (tag) {
                case HPROSE_TAG_RESULT:
                    if (mode == HPROSE_RESULT_MODE_SERIALIZED) {
                        hprose_raw_reader raw_reader = { reader.stream };
                        hprose_bytes_io result;
                        hprose_bytes_io_init(&result, NULL, 0);
                        _hprose_raw_reader_read_raw(&raw_reader, &result);
#if PHP_MAJOR_VERSION < 7
                        RETVAL_STRINGL_0(HB_BUF(result), HB_LEN(result));
#else
                        RETVAL_STR(HB_STR(result));
#endif
                    }
                    else {
                        hprose_reader_reset(&reader);
                        hprose_reader_unserialize(&reader, return_value TSRMLS_CC);
                    }
                    break;
                case HPROSE_TAG_ARGUMENT: {
                    zval *_args;
                    int32_t n, i;
                    hprose_zval_new(_args);
                    hprose_reader_reset(&reader);
                    hprose_reader_read_list(&reader, _args TSRMLS_CC);
                    n = MIN(Z_ARRLEN_P(args), Z_ARRLEN_P(_args));
                    for (i = 0; i < n; ++i) {
                        zval *val = php_array_get(_args, i);
#if PHP_MAJOR_VERSION < 7
                        Z_ADDREF_P(val);
#else
                        Z_TRY_ADDREF_P(val);
#endif                        
                        add_index_zval(args, i, val);
                    }
                    hprose_zval_free(_args);
                    break;
                }
                case HPROSE_TAG_ERROR: {
                    hprose_reader_reset(&reader);
                    zval *errstr;
                    hprose_zval_new(errstr);
                    hprose_reader_read_string(&reader, errstr TSRMLS_CC);
                    zend_throw_exception_ex(NULL, 0 TSRMLS_CC,
                                            "%s", Z_STRVAL_P(errstr));
                    hprose_reader_destroy(&reader);
                    hprose_zval_free(errstr);
                    return;
                }
                default:
                    zend_throw_exception_ex(NULL, 0 TSRMLS_CC,
                                            "Wrong Response:\r\n%s", Z_STRVAL_P(response));
                    hprose_reader_destroy(&reader);
                    return;
            }
        }
        hprose_reader_destroy(&reader);
    }
}

static zend_always_inline void hprose_client_sync_invoke(zval *client, zval *name, zval *args, zend_bool byref, int mode, zend_bool simple, zval *return_value TSRMLS_DC) {
    zval *context, *userdata, *request, response;
    hprose_client *_this = HPROSE_GET_OBJECT_P(client, client)->_this;
    hprose_zval_new(context);
    hprose_zval_new(userdata);
    object_init(context);
    object_init(userdata);
    add_property_zval(context, "client", client);
    add_property_zval(context, "userdata", userdata);
    hprose_zval_new(request);
    hprose_client_do_output(_this, name, args, byref, simple, context, request TSRMLS_CC);
    if (EG(exception)) {
        hprose_zval_free(request);
        hprose_zval_free(context);
        hprose_zval_free(userdata);
        return;
    }
    ZVAL_NULL(&response);
    method_invoke(client, sendAndReceive, &response, "z", request);
    hprose_zval_free(request);
    if (EG(exception)) {
        zval_dtor(&response);
        hprose_zval_free(context);
        hprose_zval_free(userdata);
        return;
    }
    hprose_client_do_input(_this, &response, args, mode, context, return_value TSRMLS_CC);
    zval_dtor(&response);
    hprose_zval_free(context);
    hprose_zval_free(userdata);
}

static zend_always_inline void hprose_client_async_invoke(zval *client, zval *name, zval *args, zend_bool byref, int mode, zend_bool simple, zval *callback TSRMLS_DC) {
    zval *context, *userdata, *request, *use;
    hprose_client *_this = HPROSE_GET_OBJECT_P(client, client)->_this;
    hprose_zval_new(context);
    hprose_zval_new(userdata);
    object_init(context);
    object_init(userdata);
    add_property_zval(context, "client", client);
    add_property_zval(context, "userdata", userdata);
    hprose_zval_new(request);
    hprose_client_do_output(_this, name, args, byref, simple, context, request TSRMLS_CC);
    if (EG(exception)) {
        hprose_zval_free(request);
        hprose_zval_free(context);
        hprose_zval_free(userdata);
        return;
    }
#if PHP_MAJOR_VERSION < 7
    Z_ADDREF_P(args);
    Z_ADDREF_P(context);
    Z_ADDREF_P(callback);
#else
    Z_TRY_ADDREF_P(args);
    Z_TRY_ADDREF_P(context);
    Z_TRY_ADDREF_P(callback);
#endif
    hprose_zval_new(use);
    array_init(use);
    add_next_index_zval(use, args);
    add_next_index_zval(use, context);
    add_next_index_zval(use, callback);
    add_next_index_long(use, mode);
    method_invoke(client, asyncSendAndReceive, NULL, "zz", request, use);
    hprose_zval_free(request);
    hprose_zval_free(context);
    hprose_zval_free(userdata);
    hprose_zval_free(use);
}

static zend_always_inline void hprose_client_send_and_receive_callback(hprose_client *_this, zval *response, zval *err, zval *use TSRMLS_DC) {
    zval *args = php_array_get(use, 0);
    zval *context = php_array_get(use, 1);
    zval *callback = php_array_get(use, 2);
    zend_fcall_info_cache fcc = _get_fcall_info_cache(callback TSRMLS_CC);
    uint32_t n = fcc.function_handler->common.num_args;
    long mode = HPROSE_RESULT_MODE_NORMAL;
    zval *result;
    hprose_zval_new(result);
    ZVAL_NULL(result);
    php_array_get_long(use, 3, &mode);
    if (n == 3) {
        if (err == NULL) {
            hprose_client_do_input(_this, response, args, mode, context, result TSRMLS_CC);
            if (EG(exception)) {
#if PHP_MAJOR_VERSION < 7
                err = EG(exception);
                Z_ADDREF_P(err);
                SEPARATE_ZVAL(&err);
                zend_clear_exception(TSRMLS_C);
                __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "zzz", result, args, err);
                zval_ptr_dtor(&err);
#else
                zval e;
                ZVAL_OBJ(&e, EG(exception));
                Z_ADDREF(e);
                SEPARATE_ZVAL(&e);
                zend_clear_exception();
                __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "zzz", result, args, &e);
                zval_ptr_dtor(&e);
#endif
            }
            else {
                __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "zzz", result, args, NULL);
            }
        }
        else {
            __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "zzz", result, args, err);
        }
    }
    else {
        if (err != NULL) {
            hprose_zval_free(result);
            zend_throw_exception_object(err TSRMLS_CC);
            return;
        }
        hprose_client_do_input(_this, response, args, mode, context, result TSRMLS_CC);
        if (EG(exception)) {
            hprose_zval_free(result);
            return;
        }
        switch (n) {
            case 0: __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, ""); break;
            case 1: __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "z", result); break;
            case 2: __function_invoke(fcc, NULL, NULL, 0 TSRMLS_CC, "zz", result, args); break;
        }
    }
    hprose_zval_free(result);
}

ZEND_METHOD(hprose_proxy, __construct) {
    zval *client = NULL;
    char *ns = "";
    length_t len = 0;
    HPROSE_THIS(proxy);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|s", &client, get_hprose_client_ce(), &ns, &len) == FAILURE) {
        return;
    }
    Z_ADDREF_P(client);
#if PHP_MAJOR_VERSION < 7
    _this->client = client;
#else
    _this->client = Z_OBJ_P(client);
#endif
    _this->ns = ns;
}

ZEND_METHOD(hprose_proxy, __call) {
    char *name;
    length_t len;
    zval *args;
    int32_t n;
    hprose_bytes_io _name;
    zval *__name;
    HPROSE_THIS(proxy);
#if PHP_MAJOR_VERSION >= 7
    zval client;
    ZVAL_OBJ(&client, _this->client);
#endif
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sa", &name, &len, &args) == FAILURE) {
        return;
    }
    hprose_bytes_io_init(&_name, NULL, 0);
    hprose_bytes_io_write(&_name, _this->ns, strlen(_this->ns));
    hprose_bytes_io_write(&_name, name, len);
    hprose_zval_new(__name);
#if PHP_MAJOR_VERSION < 7
    ZVAL_STRINGL_0(__name, HB_BUF(_name), HB_LEN(_name));
#else
    ZVAL_STR(__name, HB_STR(_name));
#endif
    n = Z_ARRLEN_P(args);
    if (n > 0) {
        zval *callback = php_array_get(args, n - 1);
        
        if (is_callable_p(callback)) {
#if PHP_MAJOR_VERSION < 7
            zend_hash_index_del(Z_ARRVAL_P(args), n - 1);
            hprose_client_async_invoke(_this->client, __name, args, 0, HPROSE_RESULT_MODE_NORMAL, 0, callback TSRMLS_CC);
#else
            zval _callback;
            ZVAL_COPY(&_callback, callback);
            zend_hash_index_del(Z_ARRVAL_P(args), n - 1);
            hprose_client_async_invoke(&client, __name, args, 0, HPROSE_RESULT_MODE_NORMAL, 0, &_callback TSRMLS_CC);
            zval_ptr_dtor(&_callback);
#endif
            hprose_zval_free(__name);
            return;
        }
    }
#if PHP_MAJOR_VERSION < 7
    hprose_client_sync_invoke(_this->client, __name, args, 0, HPROSE_RESULT_MODE_NORMAL, 0, return_value TSRMLS_CC);
#else
    hprose_client_sync_invoke(&client, __name, args, 0, HPROSE_RESULT_MODE_NORMAL, 0, return_value TSRMLS_CC);
#endif
    hprose_zval_free(__name);
}

ZEND_METHOD(hprose_proxy, __get) {
    hprose_bytes_io _name;
    char *name;
    length_t len;
    HPROSE_THIS(proxy);
#if PHP_MAJOR_VERSION >= 7
    zval client;
    ZVAL_OBJ(&client, _this->client);
#endif
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &name, &len) == FAILURE) {
        return;
    }
    hprose_bytes_io_init(&_name, NULL, 0);
    hprose_bytes_io_write(&_name, _this->ns, strlen(_this->ns));
    hprose_bytes_io_write(&_name, name, len);
    hprose_bytes_io_putc(&_name, '-');
#if PHP_MAJOR_VERSION < 7
    create_php_object(HproseProxy, return_value, "zs", _this->client, HB_BUF(_name), HB_LEN(_name));
#else
    create_php_object(HproseProxy, return_value, "zs", &client, HB_BUF(_name), HB_LEN(_name));
#endif
    hprose_bytes_io_close(&_name);    
}

ZEND_BEGIN_ARG_INFO_EX(hprose_proxy_construct_arginfo, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, HproseClient, 0)
    ZEND_ARG_INFO(0, ns)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_proxy_call_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_ARRAY_INFO(0, args, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_proxy_get_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, name)
ZEND_END_ARG_INFO()


static zend_function_entry hprose_proxy_methods[] = {
    ZEND_ME(hprose_proxy, __construct, hprose_proxy_construct_arginfo, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    ZEND_ME(hprose_proxy, __call, hprose_proxy_call_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_proxy, __get, hprose_proxy_get_arginfo, ZEND_ACC_PUBLIC)
    ZEND_FE_END
};

HPROSE_OBJECT_HANDLERS(proxy)

HPROSE_OBJECT_FREE_BEGIN(proxy)
    if (intern->_this) {
#if PHP_MAJOR_VERSION < 7
        zval_ptr_dtor(&(intern->_this->client));
#else
        OBJ_RELEASE(intern->_this->client);
#endif
        efree(intern->_this);
        intern->_this = NULL;
    }
HPROSE_OBJECT_FREE_END

HPROSE_OBJECT_NEW_EX_BEGIN(proxy)
    intern->_this = emalloc(sizeof(hprose_proxy));
HPROSE_OBJECT_NEW_EX_END(proxy)

HPROSE_OBJECT_NEW(proxy)

HPROSE_OBJECT_CLONE_BEGIN(proxy)
HPROSE_OBJECT_CLONE_END

HPROSE_CLASS_ENTRY(proxy)

HPROSE_STARTUP_FUNCTION(proxy) {
    HPROSE_REGISTER_CLASS("Hprose", "Proxy", proxy);
    HPROSE_REGISTER_CLASS_HANDLERS(proxy);
    return SUCCESS;
}

ZEND_METHOD(hprose_client, __construct) {
    char *url = "";
    length_t len = 0;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s", &url, &len) == FAILURE) {
        return;
    }
#if PHP_MAJOR_VERSION < 7
    _this->client = getThis();
#else
    _this->client = Z_OBJ_P(getThis());
#endif
    zend_update_property_stringl(get_hprose_client_ce(), getThis(), ZEND_STRL("url"), url, len TSRMLS_CC);
}

ZEND_METHOD(hprose_client, sendAndReceive) {
    zend_throw_exception(NULL, "This client can't support synchronous invoke.", 0 TSRMLS_CC);
}

ZEND_METHOD(hprose_client, asyncSendAndReceive) {
    zend_throw_exception(NULL, "This client can't support asynchronous invoke.", 0 TSRMLS_CC);
}

ZEND_METHOD(hprose_client, sendAndReceiveCallback) {
    zval *response, *err, *use;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz!a", &response, &err, &use) == FAILURE) {
        return;
    }
#if PHP_MAJOR_VERSION < 7
    Z_ADDREF_P(response);
    SEPARATE_ZVAL(&response);
#endif
    hprose_client_send_and_receive_callback(_this, response, err, use TSRMLS_CC);
#if PHP_MAJOR_VERSION < 7
    hprose_zval_free(response);
#endif
}

ZEND_METHOD(hprose_client, useService) {
    char *url = "", *ns = "";
    length_t url_len = 0, ns_len = 0;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|ss", &url, &url_len, &ns, &ns_len) == FAILURE) {
        return;
    }
    if (url && url_len > 0) {
        zend_update_property_stringl(get_hprose_client_ce(), getThis(), ZEND_STRL("url"), url, url_len TSRMLS_CC);
    }
    if (ns && ns_len > 0) {
        ns = estrndup(ns, ns_len + 1);
        ns[ns_len] = '_';
        create_php_object(HproseProxy, return_value, "zs", getThis(), ns, ns_len + 1);
        efree(ns);
    }
    else {
        create_php_object(HproseProxy, return_value, "z", getThis());
    }
}

ZEND_METHOD(hprose_client, invoke) {
    HPROSE_THIS(client);
    zval *name, *args = NULL, *callback = NULL;
    zend_bool byref = 0;
    zend_bool simple = _this->simple;
    zend_bool null_args = 0;
    long mode = HPROSE_RESULT_MODE_NORMAL;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|ablbz!", &name, &args, &byref, &mode, &simple, &callback) == FAILURE) {
        return;
    }
    convert_to_string(name);
    if (args == NULL) {
        hprose_zval_new(args);
        array_init(args);
        Z_ADDREF_P(args);
        null_args = 1;
    }
    if (is_callable_p(callback)) {
        hprose_client_async_invoke(getThis(), name, args, byref, mode, simple, callback TSRMLS_CC);
    }
    else {
        hprose_client_sync_invoke(getThis(), name, args, byref, mode, simple, return_value TSRMLS_CC);        
    }
    if (null_args) {
        hprose_zval_free(args);
    }
}

ZEND_METHOD(hprose_client, getFilter) {
    HPROSE_THIS(client);
    if (Z_ARRLEN_P(_this->filters)) {
        zval *filter = php_array_get(_this->filters, 0);
        RETURN_ZVAL(filter, 1, 0);
    }
    RETURN_NULL();
}    

ZEND_METHOD(hprose_client, setFilter) {
    zval *filter = NULL;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o!", &filter) == FAILURE) {
        return;
    }
    zend_hash_clean(Z_ARRVAL_P(_this->filters));
    if (filter) {
        Z_ADDREF_P(filter);        
        add_index_zval(_this->filters, 0, filter);
    }
}

ZEND_METHOD(hprose_client, addFilter) {
    zval *filter;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o", &filter) == FAILURE) {
        return;
    }
    Z_ADDREF_P(filter);        
    add_next_index_zval(_this->filters, filter);
}

ZEND_METHOD(hprose_client, removeFilter) {
    zval *filter;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o", &filter) == FAILURE) {
        return;
    }
    zval i;
    function_invoke(array_search, &i, "zz", filter, _this->filters);
#if PHP_MAJOR_VERSION < 7
    if ((Z_TYPE(i) == IS_BOOL && Z_BVAL(i) == 0) || Z_TYPE(i) == IS_NULL) {
#else
    if (Z_TYPE(i) == IS_FALSE || Z_TYPE(i) == IS_NULL) {
#endif
        RETURN_FALSE;
    }
    function_invoke(array_splice, _this->filters, "zzl", _this->filters, &i, 1);
    RETURN_TRUE;
}

ZEND_METHOD(hprose_client, getSimple) {
    HPROSE_THIS(client);
    RETURN_BOOL(_this->simple);
}    

ZEND_METHOD(hprose_client, setSimple) {
    zend_bool simple = 1;
    HPROSE_THIS(client);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b", &simple) == FAILURE) {
        return;
    }
    _this->simple = simple;
}

ZEND_BEGIN_ARG_INFO_EX(hprose_client_construct_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, url)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_send_and_receive_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, request)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_async_send_and_receive_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, request)
    ZEND_ARG_INFO(0, use)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_send_and_receive_callback_arginfo, 0, 0, 3)
    ZEND_ARG_INFO(0, response)
    ZEND_ARG_OBJ_INFO(0, err, Exception, 1)
    ZEND_ARG_ARRAY_INFO(0, use, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_use_service_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, url)
    ZEND_ARG_INFO(0, ns)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_invoke_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_ARRAY_INFO(1, args, 0)
    ZEND_ARG_INFO(0, byref)
    ZEND_ARG_INFO(0, mode)
    ZEND_ARG_INFO(0, simple)
    ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_get_filter_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_set_filter_arginfo, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, filter, HproseFilter, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_add_filter_arginfo, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, filter, HproseFilter, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_remove_filter_arginfo, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, filter, HproseFilter, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_get_simple_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(hprose_client_set_simple_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, simple)
ZEND_END_ARG_INFO()

static zend_function_entry hprose_client_methods[] = {
    ZEND_ME(hprose_client, __construct, hprose_client_construct_arginfo, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    ZEND_ME(hprose_client, sendAndReceive, hprose_client_send_and_receive_arginfo, ZEND_ACC_PROTECTED)
    ZEND_ME(hprose_client, asyncSendAndReceive, hprose_client_async_send_and_receive_arginfo, ZEND_ACC_PROTECTED)
    ZEND_ME(hprose_client, sendAndReceiveCallback, hprose_client_send_and_receive_callback_arginfo, ZEND_ACC_PROTECTED)
    ZEND_ME(hprose_client, useService, hprose_client_use_service_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, invoke, hprose_client_invoke_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, getFilter, hprose_client_get_filter_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, setFilter, hprose_client_set_filter_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, addFilter, hprose_client_add_filter_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, removeFilter, hprose_client_remove_filter_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, getSimple, hprose_client_get_simple_arginfo, ZEND_ACC_PUBLIC)
    ZEND_ME(hprose_client, setSimple, hprose_client_set_simple_arginfo, ZEND_ACC_PUBLIC)
    ZEND_FE_END
};

HPROSE_OBJECT_HANDLERS(client)

HPROSE_OBJECT_FREE_BEGIN(client)
    if (intern->_this) {
        intern->_this->client = NULL;
        hprose_zval_free(intern->_this->filters);
        efree(intern->_this);
        intern->_this = NULL;
    }
HPROSE_OBJECT_FREE_END

HPROSE_OBJECT_NEW_EX_BEGIN(client)
    intern->_this = emalloc(sizeof(hprose_client));
    intern->_this->client = NULL;
    intern->_this->ns = "";
    intern->_this->simple = 0;
    hprose_zval_new(intern->_this->filters);
    array_init(intern->_this->filters);
HPROSE_OBJECT_NEW_EX_END(client)

HPROSE_OBJECT_NEW(client)

HPROSE_CLASS_ENTRY(client)

HPROSE_STARTUP_FUNCTION(client) {
    HPROSE_REGISTER_CLASS_EX("Hprose", "Client", client, get_hprose_proxy_ce(), "HproseProxy");
    HPROSE_REGISTER_CLASS_HANDLERS(client);
    zend_declare_property_stringl(hprose_client_ce, ZEND_STRL("url"), ZEND_STRL(""), ZEND_ACC_PROTECTED TSRMLS_CC);
    hprose_client_ce->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;
    return SUCCESS;
}
