#include <PoseidonOpenAL/IosAudioSession.hpp>

#ifdef POSEIDON_TARGET_IOS

#include <Poseidon/Foundation/Framework/Log.hpp>

namespace
{
using ObjCId = void*;
using ObjCSel = void*;

extern "C"
{
ObjCId objc_getClass(const char* name);
ObjCSel sel_registerName(const char* name);
ObjCId objc_msgSend(ObjCId self, ObjCSel op, ...);

extern ObjCId AVAudioSessionCategoryPlayback;
extern ObjCId AVAudioSessionModeDefault;
}

template <typename Fn>
Fn MsgSend()
{
    return reinterpret_cast<Fn>(objc_msgSend);
}

const char* CStringFromNSString(ObjCId string)
{
    if (!string)
        return "";
    const char* text = MsgSend<const char* (*)(ObjCId, ObjCSel)>()(string, sel_registerName("UTF8String"));
    return text ? text : "";
}

const char* AudioSessionRoute(ObjCId session)
{
    ObjCId route = MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(session, sel_registerName("currentRoute"));
    ObjCId description = route ? MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(route, sel_registerName("description")) : nullptr;
    return CStringFromNSString(description);
}

const char* ErrorDescription(ObjCId error)
{
    ObjCId description = error ? MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(error, sel_registerName("localizedDescription")) : nullptr;
    return CStringFromNSString(description);
}
} // namespace

namespace Poseidon
{

bool ConfigureIOSAudioSessionForPlayback()
{
    ObjCId sessionClass = objc_getClass("AVAudioSession");
    if (!sessionClass)
    {
        LOG_ERROR(Audio, "iOS audio session: AVAudioSession class unavailable");
        return false;
    }

    ObjCId session = MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(sessionClass, sel_registerName("sharedInstance"));
    if (!session)
    {
        LOG_ERROR(Audio, "iOS audio session: sharedInstance unavailable");
        return false;
    }

    ObjCId error = nullptr;
    bool categoryOk = MsgSend<bool (*)(ObjCId, ObjCSel, ObjCId, ObjCId, unsigned long, ObjCId*)>()(
        session,
        sel_registerName("setCategory:mode:options:error:"),
        AVAudioSessionCategoryPlayback,
        AVAudioSessionModeDefault,
        0,
        &error);
    if (!categoryOk)
    {
        LOG_ERROR(Audio, "iOS audio session: setCategory playback failed: {}", ErrorDescription(error));
        return false;
    }

    error = nullptr;
    bool activeOk = MsgSend<bool (*)(ObjCId, ObjCSel, bool, ObjCId*)>()(
        session,
        sel_registerName("setActive:error:"),
        true,
        &error);
    if (!activeOk)
    {
        LOG_ERROR(Audio, "iOS audio session: setActive failed: {}", ErrorDescription(error));
        return false;
    }

    ObjCId category = MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(session, sel_registerName("category"));
    ObjCId mode = MsgSend<ObjCId (*)(ObjCId, ObjCSel)>()(session, sel_registerName("mode"));
    const bool otherAudioPlaying =
        MsgSend<bool (*)(ObjCId, ObjCSel)>()(session, sel_registerName("isOtherAudioPlaying"));
    const bool silenced =
        MsgSend<bool (*)(ObjCId, ObjCSel)>()(session, sel_registerName("secondaryAudioShouldBeSilencedHint"));

    LOG_INFO(Audio, "iOS audio session active: category='{}' mode='{}' otherAudio={} silencedHint={} route={}",
             CStringFromNSString(category), CStringFromNSString(mode), otherAudioPlaying ? "yes" : "no",
             silenced ? "yes" : "no", AudioSessionRoute(session));

    return activeOk;
}

} // namespace Poseidon

#else

namespace Poseidon
{

bool ConfigureIOSAudioSessionForPlayback()
{
    return true;
}

} // namespace Poseidon

#endif
