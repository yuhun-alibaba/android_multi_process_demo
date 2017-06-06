#include "Buffering/IPCBuffer.h"
#include "IPCArguments.h"
#include "IPCByteArray.h"
#include "IPCException.h"
#include "IPCHandler.h"
#include "IPCMessageJS.h"
#include "IPCResult.h"
#include "IPCSender.h"
#include "IPCString.h"
#include "LogUtils.h"
#include "Serializing/IPCSerializer.h"
#include "WeexJSConnection.h"
#include <jni.h>
#include <unistd.h>

namespace {

class ScopedJStringUTF8 {
public:
    ScopedJStringUTF8(JNIEnv* env, jstring);
    ~ScopedJStringUTF8();
    const char* getChars();

private:
    JNIEnv* m_env;
    jstring m_jstring;
    const char* m_chars;
};

class ScopedJString {
public:
    ScopedJString(JNIEnv* env, jstring);
    ~ScopedJString();
    const jchar* getChars();
    size_t getCharsLength();

private:
    JNIEnv* m_env;
    jstring m_jstring;
    const uint16_t* m_chars;
    size_t m_len;
};

ScopedJStringUTF8::ScopedJStringUTF8(JNIEnv* env, jstring _jstring)
    : m_env(env)
    , m_jstring(_jstring)
    , m_chars(nullptr)
{
}

ScopedJStringUTF8::~ScopedJStringUTF8()
{
    if (m_chars)
        m_env->ReleaseStringUTFChars(m_jstring, m_chars);
}

const char* ScopedJStringUTF8::getChars()
{
    if (m_chars)
        return m_chars;
    m_chars = m_env->GetStringUTFChars(m_jstring, nullptr);
    return m_chars;
}

ScopedJString::ScopedJString(JNIEnv* env, jstring _jstring)
    : m_env(env)
    , m_jstring(_jstring)
    , m_chars(nullptr)
    , m_len(0)
{
}

ScopedJString::~ScopedJString()
{
    if (m_chars)
        m_env->ReleaseStringChars(m_jstring, m_chars);
}

const jchar*
ScopedJString::getChars()
{
    if (m_chars)
        return m_chars;
    m_chars = m_env->GetStringChars(m_jstring, nullptr);
    m_len = m_env->GetStringLength(m_jstring);
    return m_chars;
}

size_t
ScopedJString::getCharsLength()
{
    if (m_chars)
        return m_len;
    m_len = m_env->GetStringLength(m_jstring);
    return m_len;
}
}

static JNIEnv* getJNIEnv();

static jclass jBridgeClazz;

static jmethodID jCallNativeMethodId;
static jmethodID jLogMethodId;

static jobject jThis;
static jobject jScript;
static JavaVM* sVm = NULL;
static IPCSender* sSender;
static std::unique_ptr<IPCHandler> sHandler;
static std::unique_ptr<WeexJSConnection> sConnection;

JNIEnv* getJNIEnv()
{
    JNIEnv* env = NULL;
    if ((sVm)->GetEnv((void**)&env, JNI_VERSION_1_4) != JNI_OK) {
        return JNI_FALSE;
    }
    return env;
}

static jstring getArgumentAsJString(JNIEnv* env, IPCArguments* arguments, int argument)
{
    jstring ret = nullptr;
    if (arguments->getType(argument) == IPCType::STRING) {
        const IPCString* s = arguments->getString(argument);
        ret = env->NewString(s->content, s->length);
    }
    return ret;
}

static jbyteArray getArgumentAsJByteArray(JNIEnv* env, IPCArguments* arguments, size_t argument)
{
    jbyteArray ba = nullptr;
    if (argument >= arguments->getCount())
        return nullptr;
    if (arguments->getType(argument) == IPCType::BYTEARRAY) {
        const IPCByteArray* ipcBA = arguments->getByteArray(argument);
        int strLen = ipcBA->length;
        ba = env->NewByteArray(strLen);
        env->SetByteArrayRegion(ba, 0, strLen,
            reinterpret_cast<const jbyte*>(ipcBA->content));
    }
    return ba;
}

//基础函数部分


// ipc处理部分
static std::unique_ptr<IPCResult> handleCallNative(IPCArguments* arguments)
{
    JNIEnv* env = getJNIEnv();
    //instacneID args[0]
    jstring jInstanceId = getArgumentAsJString(env, arguments, 0);
    if (jCallNativeMethodId == NULL) {
        jCallNativeMethodId = env->GetMethodID(jBridgeClazz,
            "callNative",
            "(Ljava/lang/String;)I");
    }

    int flag = env->CallIntMethod(jThis, jCallNativeMethodId, jInstanceId);
    if (flag == -1) {
        LOGE("instance destroy JFM must stop callNative");
    }

    env->DeleteLocalRef(jInstanceId);
    return createInt32Result(flag);
}


static std::unique_ptr<IPCResult> handleCallNativeLog(IPCArguments* arguments)
{
    JNIEnv* env = getJNIEnv();
    bool result = false;
    jstring str_msg = getArgumentAsJString(env, arguments, 0);
    if (jLogMethodId == NULL) {
        jLogMethodId = env->GetMethodID(jBridgeClazz,
                "callNativeLog",
                "(Ljava/lang/String;)I");
    }
     int flag = env->CallIntMethod(jThis, jCallNativeMethodId, str_msg);
     if (flag == -1) {
         LOGE("instance destroy JFM must stop callNative");
     }
     env->DeleteLocalRef(str_msg);
     return createInt32Result(flag);

    /*if (jWXLogUtils != NULL) {
        if (jLogMethodId == NULL) {
            jLogMethodId = env->GetStaticMethodID(jWXLogUtils, "d", "(Ljava/lang/String;Ljava/lang/String;)V");
        }
        if (jLogMethodId != NULL) {
            jstring str_tag = env->NewStringUTF("jsLog");
            // str_msg = env->NewStringUTF(s);
            env->CallStaticVoidMethod(jWXLogUtils, jLogMethodId, str_tag, str_msg);
            result = true;
            env->DeleteLocalRef(str_msg);
            env->DeleteLocalRef(str_tag);
        }
    }*/
}

static void initHandler(IPCHandler* handler)
{
    handler->registerHandler(static_cast<uint32_t>(IPCProxyMsg::CALLNATIVE), handleCallNative);
    handler->registerHandler(static_cast<uint32_t>(IPCProxyMsg::NATIVELOG), handleCallNativeLog);
}

static void addString(JNIEnv* env, IPCSerializer* serializer, jstring str)
{
    ScopedJString scopedString(env, str);
    const uint16_t* chars = scopedString.getChars();
    size_t charsLength = scopedString.getCharsLength();
    serializer->add(chars, charsLength);
}

static void addJSONString(JNIEnv* env, IPCSerializer* serializer, jstring str)
{
    ScopedJString scopedString(env, str);
    const uint16_t* chars = scopedString.getChars();
    size_t charsLength = scopedString.getCharsLength();
    serializer->addJSON(chars, charsLength);
}

static jint doInitFramework(JNIEnv* env,
    jobject object,
    jstring script)
{
    try {
        LOGE("shiwentao Weexproxy send IPCJSMsg::INITFRAMEWORK");
        sHandler = std::move(createIPCHandler());
        sConnection.reset(new WeexJSConnection());
        sSender = sConnection->start(sHandler.get());
        initHandler(sHandler.get());
        std::unique_ptr<IPCSerializer> serializer(createIPCSerializer());
        serializer->setMsg(static_cast<uint32_t>(IPCJSMsg::INITFRAMEWORK));

        ScopedJString scopedString(env, script);
        const jchar* chars = scopedString.getChars();
        int charLength = scopedString.getCharsLength();
        serializer->add(chars, charLength);

        std::unique_ptr<IPCBuffer> buffer = serializer->finish();
        std::unique_ptr<IPCResult> result = sSender->send(buffer.get());
        if (result->getType() != IPCType::INT32) {
            LOGE("shiwentao initFramework Unexpected result type");
            return false;
        }
        return result->get<jint>();
    } catch (IPCException& e) {
        LOGE("shiwentao initFramework error: %s", e.msg());
        LOGE("%s", e.msg());
        return false;
    }
    return true;
}

static jint native_initFramework(JNIEnv* env,
    jobject object,
    jstring script)
{
    jThis = env->NewGlobalRef(object);
    jScript = env->NewGlobalRef(script);
    return doInitFramework(env, jThis, static_cast<jstring>(jScript));
}

/**
 * Called to execute JavaScript such as . createInstance(),destroyInstance ext.
 *
 */
static jint native_execJS(JNIEnv* env,
    jobject jthis,
    jstring jinstanceid)
{
    try {
        LOGE("shiwentao native_execJS");
        std::unique_ptr<IPCSerializer> serializer(createIPCSerializer());
        serializer->setMsg(static_cast<uint32_t>(IPCJSMsg::EXECJS));

        std::unique_ptr<IPCBuffer> buffer = serializer->finish();
        std::unique_ptr<IPCResult> result = sSender->send(buffer.get());
        if (result->getType() != IPCType::INT32) {
            LOGE("execJS Unexpected result type");
            return false;
        }
        return result->get<jint>();
    } catch (IPCException& e) {
        LOGE("%s", e.msg());
        return false;
    }
    return true;
}

static const char* gBridgeClassPathName = "com/multiprocessdemo/DemoManger";
static JNINativeMethod gMethods[] = {
    { "initFramework",
        "(Ljava/lang/String;)I",
        (void*)native_initFramework },
    { "execJS",
        "(Ljava/lang/String;)I",
        (void*)native_execJS }
};

static int
registerBridgeNativeMethods(JNIEnv* env, JNINativeMethod* methods, int numMethods)
{
    if (jBridgeClazz == NULL) {
        LOGE("registerBridgeNativeMethods failed to find bridge class.");
        return JNI_FALSE;
    }
    if ((env)->RegisterNatives(jBridgeClazz, methods, numMethods) < 0) {
        LOGE("registerBridgeNativeMethods failed to register native methods for bridge class.");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

static bool registerNatives(JNIEnv* env)
{
    if (JNI_TRUE != registerBridgeNativeMethods(env, gMethods, sizeof(gMethods) / sizeof(gMethods[0])))
        return false;
    return true;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    LOGE("shiwentao begin JNI_OnLoad");
    JNIEnv* env;
    /* Get environment */
    if ((vm)->GetEnv((void**)&env, JNI_VERSION_1_4) != JNI_OK) {
        return JNI_FALSE;
    }

    sVm = vm;
    jclass tempClass = env->FindClass(
        "weex/alibaba/com/multiprocessdemo/DemoManger");
    jBridgeClazz = (jclass)env->NewGlobalRef(tempClass);


    env->DeleteLocalRef(tempClass);
    if (!registerNatives(env)) {
        return JNI_FALSE;
    }

    LOGE("shiwentao end JNI_OnLoad");
    return JNI_VERSION_1_4;
}

void JNI_OnUnload(JavaVM* vm, void* reserved)
{
    LOGD("beigin JNI_OnUnload");
    JNIEnv* env;

    /* Get environment */
    if ((vm)->GetEnv((void**)&env, JNI_VERSION_1_4) != JNI_OK) {
        return;
    }
    env->DeleteGlobalRef(jBridgeClazz);
    if (jThis)
        env->DeleteGlobalRef(jThis);
    sConnection.reset();
    LOGD(" end JNI_OnUnload");
}
