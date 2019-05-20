#include <jni.h>
#include <string>
#include "test_server.cpp"

#define JNI_API(retType,funName,...) extern "C"  JNIEXPORT retType Java_com_zlmediakit_jni_ZLMediaKit_##funName(JNIEnv* env, jclass cls,##__VA_ARGS__)
#define MediaPlayerCallBackSign "com/zlmediakit/jni/ZLMediaKit$MediaPlayerCallBack"
#define MediaFrameSign "com/zlmediakit/jni/ZLMediaKit$MediaFrame"


string stringFromJstring(JNIEnv *env,jstring jstr){
    if(!env || !jstr){
        WarnL << "invalid args";
        return "";
    }
    const char *field_char = env->GetStringUTFChars(jstr, 0);
    string ret(field_char,env->GetStringUTFLength(jstr));
    env->ReleaseStringUTFChars(jstr, field_char);
    return ret;
}
string stringFromJbytes(JNIEnv *env,jbyteArray jbytes){
    if(!env || !jbytes){
        WarnL << "invalid args";
        return "";
    }
    jbyte *bytes = env->GetByteArrayElements(jbytes, 0);
    string ret((char *)bytes,env->GetArrayLength(jbytes));
    env->ReleaseByteArrayElements(jbytes,bytes,0);
    return ret;
}
string stringFieldFromJava(JNIEnv *env,  jobject jdata,jfieldID jid){
    if(!env || !jdata || !jid){
        WarnL << "invalid args";
        return "";
    }
    jstring field_str = (jstring)env->GetObjectField(jdata,jid);
    auto ret = stringFromJstring(env,field_str);
    env->DeleteLocalRef(field_str);
    return ret;
}

string bytesFieldFromJava(JNIEnv *env,  jobject jdata,jfieldID jid){
    if(!env || !jdata || !jid){
        WarnL << "invalid args";
        return "";
    }
    jbyteArray jbufArray = (jbyteArray)env->GetObjectField(jdata, jid);
    string ret = stringFromJbytes(env,jbufArray);
    env->DeleteLocalRef(jbufArray);
    return ret;
}

jstring jstringFromString(JNIEnv* env, const char* pat) {
    return (jstring)env->NewStringUTF(pat);
}

jbyteArray jbyteArrayFromString(JNIEnv* env, const char* pat,int len = 0){
    if(len <= 0){
        len = strlen(pat);
    }
    jbyteArray jarray = env->NewByteArray(len);
    env->SetByteArrayRegion(jarray, 0, len, (jbyte *)(pat));
    return jarray;
}

jobject makeJavaFrame(JNIEnv* env,const Frame::Ptr &frame){
    static jclass jclass_obj = (jclass)env->NewGlobalRef(env->FindClass(MediaFrameSign));
    static jmethodID jmethodID_init = env->GetMethodID(jclass_obj, "<init>", "()V");
    static jfieldID jfieldID_dts = env->GetFieldID(jclass_obj,"dts","I");
    static jfieldID jfieldID_pts = env->GetFieldID(jclass_obj,"pts","I");
    static jfieldID jfieldID_prefixSize = env->GetFieldID(jclass_obj,"prefixSize","I");
    static jfieldID jfieldID_keyFrame = env->GetFieldID(jclass_obj,"keyFrame","Z");
    static jfieldID jfieldID_data = env->GetFieldID(jclass_obj,"data","[B");
    static jfieldID jfieldID_trackType = env->GetFieldID(jclass_obj,"trackType","I");
    static jfieldID jfieldID_codecId = env->GetFieldID(jclass_obj,"codecId","I");

    if(!frame){
        return nullptr;
    }
    jobject ret = env->NewObject(jclass_obj, jmethodID_init);
    env->SetIntField(ret,jfieldID_dts,frame->dts());
    env->SetIntField(ret,jfieldID_pts,frame->pts());
    env->SetIntField(ret,jfieldID_prefixSize,frame->prefixSize());
    env->SetBooleanField(ret,jfieldID_keyFrame,frame->keyFrame());
    env->SetObjectField(ret,jfieldID_data,jbyteArrayFromString(env,frame->data(),frame->size()));
    env->SetIntField(ret,jfieldID_trackType,frame->getTrackType());
    env->SetIntField(ret,jfieldID_codecId,frame->getCodecId());
    return ret;
}

static JavaVM *s_jvm = nullptr;

template <typename FUN>
void doInJavaThread(FUN &&fun){
    JNIEnv *env;
    int status = s_jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (status != JNI_OK) {
        if (s_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
            return;
        }
    }
    fun(env);
    if (status != JNI_OK) {
        //Detach线程
        s_jvm->DetachCurrentThread();
    }
}

#define emitEvent(delegate,method,argFmt,...) \
{ \
    doInJavaThread([&](JNIEnv* env) { \
        static jclass cls = env->GetObjectClass(delegate); \
        static jmethodID jmid = env->GetMethodID(cls, method, argFmt); \
        jobject localRef = env->NewLocalRef(delegate); \
        if(localRef){ \
            env->CallVoidMethod(localRef, jmid, ##__VA_ARGS__); \
        }else{ \
            WarnL << "弱引用已经释放:" << method << " " << argFmt; \
        }\
    }); \
}

/*
 * 加载动态库
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    //设置日志
    s_jvm = vm;
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());
    InfoL;
    return JNI_VERSION_1_6;
}
/*
 * 卸载动态库
 */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved){
    InfoL;
    s_sem.post();
}


JNI_API(jboolean,startDemo,jstring path_to_jni_file){
    string sd_path = stringFromJstring(env,path_to_jni_file);
    string jni_file = sd_path +  "/zlmediakit.jni";

    DebugL << "sd_path:" << sd_path;
    DebugL << "jni file:" << jni_file;

    static thread s_th([sd_path,jni_file](){
        try {
            mINI::Instance()[Http::kRootPath] = mINI::Instance()[Hls::kFilePath] = sd_path + "/httpRoot";
            do_main(jni_file);
        }catch (std::exception &ex){
            WarnL << ex.what();
        }
    });

    static onceToken s_token([]{
        s_th.detach();
    });
    return true;
};

JNI_API(jlong,createMediaPlayer,jstring url,jobject callback){
    static auto loadFrameClass = makeJavaFrame(env,nullptr);
    MediaPlayer::Ptr *ret = new MediaPlayer::Ptr(new MediaPlayer());
    MediaPlayer::Ptr &player = *ret;

    weak_ptr<MediaPlayer> weakPlayer = player;
    jobject globalWeakRef = env->NewWeakGlobalRef(callback);
    player->setOnPlayResult([weakPlayer,globalWeakRef](const SockException &ex) {
        auto strongPlayer = weakPlayer.lock();
        if (!strongPlayer) {
            return;
        }
        emitEvent((jobject)globalWeakRef,"onPlayResult","(ILjava/lang/String;)V",(jint)ex.getErrCode(),env->NewStringUTF(ex.what()));

        if(ex){
            return;
        }

        auto viedoTrack = strongPlayer->getTrack(TrackVideo);
        if (viedoTrack) {
            viedoTrack->addDelegate(std::make_shared<FrameWriterInterfaceHelper>([globalWeakRef](const Frame::Ptr &frame) {
                emitEvent((jobject)globalWeakRef,"onData","(L" MediaFrameSign ";)V",makeJavaFrame(env,frame));
            }));
        }

        auto audioTrack = strongPlayer->getTrack(TrackAudio);
        if (audioTrack) {
            audioTrack->addDelegate(std::make_shared<FrameWriterInterfaceHelper>([globalWeakRef](const Frame::Ptr &frame) {
                emitEvent((jobject)globalWeakRef,"onData","(L" MediaFrameSign ";)V",makeJavaFrame(env,frame));
            }));
        }

    });

    player->setOnShutdown([globalWeakRef,weakPlayer](const SockException &ex) {
        auto strongPlayer = weakPlayer.lock();
        if (!strongPlayer) {
            return;
        }
        emitEvent((jobject)globalWeakRef,"onShutdown","(ILjava/lang/String;)V",(jint)ex.getErrCode(),env->NewStringUTF(ex.what()));
    });

    (*player)[Client::kRtpType] = Rtsp::RTP_UDP;
    player->play(stringFromJstring(env,url));

    return (jlong)(ret);
}



JNI_API(void,releaseMediaPlayer,jlong ptr){
    MediaPlayer::Ptr *player = (MediaPlayer::Ptr *)ptr;
    delete player;
}