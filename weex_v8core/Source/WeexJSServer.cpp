
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <type_traits>
#include <unistd.h>


// readline/history.h has a Function typedef which conflicts with the WTF::Function template from WTF/Forward.h
// We #define it to something else to avoid this conflict.


#include <sys/time.h>

#include <signal.h>


#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

#include "Buffering/IPCBuffer.h"
#include "IPCArguments.h"
#include "IPCByteArray.h"
#include "IPCHandler.h"
#include "IPCListener.h"
#include "IPCMessageJS.h"
#include "IPCResult.h"
#include "IPCSender.h"
#include "IPCString.h"
#include "IPCType.h"
#include "IPCFutexPageQueue.h"
#include "IPCException.h"
#include "LogUtils.h"
#include "Serializing/IPCSerializer.h"
#include "WeexJSServer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
namespace {

/*void GlobalObject::initFunction()
{
    VM& vm = this->vm();
    const HashTableValue JSEventTargetPrototypeTableValues[] = {
        { "callNative", JSC::Function, NoIntrinsic, { (intptr_t) static_cast<NativeFunction>(functionCallNative), (intptr_t)(3) } },
    };
    reifyStaticProperties(vm, JSEventTargetPrototypeTableValues, *this);
}*/


// 里面有IPC消息操作
/*EncodedJSValue JSC_HOST_CALL functionCallNative(ExecState* state)
{
    base::debug::TraceScope traceScope("weex", "callNative");

    GlobalObject* globalObject = static_cast<GlobalObject*>(state->lexicalGlobalObject());
    WeexJSServer* server = globalObject->m_server;
    IPCSender* sender = server->getSender();
    IPCSerializer* serializer = server->getSerializer();
    serializer->setMsg(static_cast<uint32_t>(IPCProxyMsg::CALLNATIVE));
    //instacneID args[0]
    getArgumentAsJString(serializer, state, 0);
    //task args[1]
    getArgumentAsJByteArray(serializer, state, 1);
    //callback args[2]
    getArgumentAsJString(serializer, state, 2);
    std::unique_ptr<IPCBuffer> buffer = serializer->finish();
    std::unique_ptr<IPCResult> result = sender->send(buffer.get());
    if (result->getType() != IPCType::INT32) {
        LOGE("functionCallNative: unexpected result: %d", result->getType());
        return JSValue::encode(jsNumber(0));
    }

    return JSValue::encode(jsNumber(result->get<int32_t>()));
}*/



/**
 * this function is to execute a section of JavaScript content.
 */
 /*
bool ExecuteJavaScript(JSGlobalObject* globalObject,
    const String& source,
    bool report_exceptions)
{
    SourceOrigin sourceOrigin(String::fromUTF8("(weex)"));
    NakedPtr<Exception> evaluationException;
    JSValue returnValue = evaluate(globalObject->globalExec(), makeSource(source, sourceOrigin), JSValue(), evaluationException);
    if (report_exceptions && evaluationException) {
        ReportException(globalObject, evaluationException.get(), "", "");
    }
    if (evaluationException)
        return false;
    globalObject->vm().drainMicrotasks();
    return true;
}

void ReportException(JSGlobalObject* _globalObject, Exception* exception, const char* instanceid,
    const char* func)
{
    String exceptionInfo = exceptionToString(_globalObject, exception->value());
    CString data = exceptionInfo.utf8();
    GlobalObject* globalObject = static_cast<GlobalObject*>(_globalObject);
    WeexJSServer* server = globalObject->m_server;
    IPCSender* sender = server->getSender();
    IPCSerializer* serializer = server->getSerializer();
    serializer->setMsg(static_cast<uint32_t>(IPCProxyMsg::REPORTEXCEPTION));
    serializer->add(instanceid, strlen(instanceid));
    serializer->add(func, strlen(func));
    serializer->add(data.data(), data.length());

    std::unique_ptr<IPCBuffer> buffer = serializer->finish();
    std::unique_ptr<IPCResult> result = sender->send(buffer.get());
    if (result->getType() != IPCType::VOID) {
        LOGE("REPORTEXCEPTION: unexpected result: %d", result->getType());
    }
}
*/

class unique_fd {
public:
    explicit unique_fd(int fd);
    ~unique_fd();
    int get() const;

private:
    int m_fd;
};

unique_fd::unique_fd(int fd)
    : m_fd(fd)
{
}

unique_fd::~unique_fd()
{
    close(m_fd);
}

int unique_fd::get() const
{
    return m_fd;
}

}

struct WeexJSServer::WeexJSServerImpl {
    WeexJSServerImpl(int fd, bool enableTrace);
    bool enableTrace;
    // RefPtr<VM> globalVM;
    std::unique_ptr<IPCFutexPageQueue> futexPageQueue;
    std::unique_ptr<IPCSender> sender;
    std::unique_ptr<IPCHandler> handler;
    std::unique_ptr<IPCListener> listener;
    std::unique_ptr<IPCSerializer> serializer;
};

WeexJSServer::WeexJSServerImpl::WeexJSServerImpl(int _fd, bool _enableTrace)
    : enableTrace(_enableTrace)
{
    void* base = mmap(nullptr, IPCFutexPageQueue::ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (base == MAP_FAILED) {
        int _errno = errno;
        close(_fd);
        throw IPCException("failed to map ashmem region: %s", strerror(_errno));
    }
    close(_fd);
    futexPageQueue.reset(new IPCFutexPageQueue(base, IPCFutexPageQueue::ipc_size, 1));
    handler = std::move(createIPCHandler());
    sender = std::move(createIPCSender(futexPageQueue.get(), handler.get()));
    listener = std::move(createIPCListener(futexPageQueue.get(), handler.get()));
    serializer = std::move(createIPCSerializer());
}

WeexJSServer::WeexJSServer(int fd, bool enableTrace)
    : m_impl(new WeexJSServerImpl(fd, enableTrace))
{
    IPCHandler* handler = m_impl->handler.get();
    handler->registerHandler(static_cast<uint32_t>(IPCJSMsg::INITFRAMEWORK), [this](IPCArguments* arguments) {
        // 收到IPC
        // 打印Log相关Log
        LOGE("shiwentao WeexJSServer receive IPCJSMsg::INITFRAMEWORK");
        return createInt32Result(static_cast<int32_t>(true));
    });

    handler->registerHandler(static_cast<uint32_t>(IPCJSMsg::EXECJS), [this](IPCArguments* arguments) {

        //此处添加IPC相关操作
        return createInt32Result(static_cast<int32_t>(true));

    });
}

WeexJSServer::~WeexJSServer()
{
}

void WeexJSServer::loop()
{
    m_impl->listener->listen();
}

IPCSender* WeexJSServer::getSender()
{
    return m_impl->sender.get();
}

IPCSerializer* WeexJSServer::getSerializer()
{
    return m_impl->serializer.get();
}
