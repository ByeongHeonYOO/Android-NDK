/*
 * Description : This uses two surface views(native windows) to display images which are captured by different two cameras.
 * reference : https://github.com/justinjoy/native-camera2.git
 *
 */

#include <cstring>
#include "messages-internal.h"

#include <iostream>
#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>

#include <media/NdkImageReader.h>


//NDK camera device 선언
static ACameraDevice *mainCameraDevice;
//capture 영상을 display 할 window
static ANativeWindow *mainNativeWindow;
//capture request 선언
static ACaptureRequest *mainCaptureRequest;
//ACaptureRequest_addTarget method 에서 결과 ANativeWindow를  ACaptureRequest에 추가 목적.
static ACameraOutputTarget *mainCameraOutputTarget;
//캡처 작업을 관리하고 입력 장치에서 캡처 출력으로의 데이터 흐름을 조정하는 객체
static ACaptureSessionOutput *mainSessionOutput;
//Set of ACaptureSessionOutput
static ACaptureSessionOutputContainer *mainCaptureSessionOutputContainer;
//Camera device frame capture session
static ACameraCaptureSession *mainCaptureSession;

static ACameraDevice_StateCallbacks deviceStateCallbacks;   //Camera device 상태에 따른 call back 함수
static ACameraCaptureSession_stateCallbacks captureSessionStateCallbacks;   //capture session 관련 call back 함수

static ACameraDevice *extraCameraDevice;
static ANativeWindow *extraViewWindow;
static ACaptureRequest *extraCaptureRequest;
static ACameraOutputTarget *extraCameraOutputTarget;
static ACaptureSessionOutput *extraSessionOutput;
static ACaptureSessionOutputContainer *extraCaptureSessionOutputContainer;
static ACameraCaptureSession *extraCaptureSession;

static void camera_device_on_disconnected(void *context, ACameraDevice *device) {
    LOGI("Camera(id: %s) is diconnected.\n", ACameraDevice_getId(device));
}

static void camera_device_on_error(void *context, ACameraDevice *device, int error) {
    LOGE("Error(code: %d) on Camera(id: %s).\n", error, ACameraDevice_getId(device));
}

static void capture_session_on_ready(void *context, ACameraCaptureSession *session) {
    LOGI("Session is ready. %p\n", session);
}

static void capture_session_on_active(void *context, ACameraCaptureSession *session) {
    LOGI("Session is activated. %p\n", session);
}

static void capture_session_on_closed(void *context, ACameraCaptureSession *session) {
    LOGI("Session is closed. %p\n", session);
}

extern "C" {
JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_stopPreview(JNIEnv *env,
                                                                                    jclass clazz);
JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_startPreview(JNIEnv *env,
                                                                                     jclass clazz,
                                                                                     jobject surface);
JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_stopExtraView(JNIEnv *env,
                                                                                    jclass clazz);
JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_startExtraView(JNIEnv *env,
                                                                                     jclass clazz,
                                                                                     jobject surface);
}

static void openCamera(int cam_num, ACameraDevice **cameraDevice)
{
    ACameraIdList *cameraIdList = NULL;
    ACameraMetadata *cameraMetadata = NULL;

    const char *selectedCameraId = NULL;
    camera_status_t camera_status = ACAMERA_OK;
    ACameraManager *cameraManager = ACameraManager_create();

    camera_status = ACameraManager_getCameraIdList(cameraManager, &cameraIdList);
    if (camera_status != ACAMERA_OK) {
        LOGE("Failed to get camera id list (reason: %d)\n", camera_status);
        return;
    }

    if (cameraIdList->numCameras < 1) {
        LOGE("No camera device detected.\n");
        return;
    }

    deviceStateCallbacks.onDisconnected = camera_device_on_disconnected;
    deviceStateCallbacks.onError = camera_device_on_error;

    //to use multiple camera
    selectedCameraId = cameraIdList->cameraIds[cam_num];

    LOGI("Trying to open Camera2 (id: %s, num of camera : %d)\n", selectedCameraId,
         cameraIdList->numCameras);

    camera_status = ACameraManager_getCameraCharacteristics(cameraManager, selectedCameraId,
                                                            &cameraMetadata);

    if (camera_status != ACAMERA_OK) {
        LOGE("Failed to get camera meta data of ID:%s\n", selectedCameraId);
    }

    ACameraMetadata_const_entry entry;
    ACameraMetadata_getConstEntry(cameraMetadata,
                                  ACAMERA_LENS_FACING , &entry);

    auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(
            entry.data.u8[0]);

    if (facing == ACAMERA_LENS_FACING_BACK) {
        LOGI("LENS FACING BACK...");
    } else if (facing == ACAMERA_LENS_FACING_FRONT) {
        LOGI("LENS FACING FRONT...");
    }

    camera_status = ACameraManager_openCamera(cameraManager, selectedCameraId,
                                              &deviceStateCallbacks, cameraDevice);

    if (camera_status != ACAMERA_OK) {
        LOGE("Failed to open camera device (id: %s)\n", selectedCameraId);
    }

    captureSessionStateCallbacks.onReady = capture_session_on_ready;
    captureSessionStateCallbacks.onActive = capture_session_on_active;
    captureSessionStateCallbacks.onClosed = capture_session_on_closed;

    ACameraMetadata_free(cameraMetadata);
    ACameraManager_deleteCameraIdList(cameraIdList);
    ACameraManager_delete(cameraManager);
}

static void closeCamera(ACameraDevice *cameraDevice, ACaptureSessionOutput *sessionOutput, ACaptureRequest *captureRequest,
                                ACameraOutputTarget *cameraOutputTarget, ACaptureSessionOutputContainer *captureSessionOutputContainer)
{
    camera_status_t camera_status = ACAMERA_OK;

    if (captureRequest != NULL) {
        ACaptureRequest_free(captureRequest);
        captureRequest = NULL;
    }

    if (cameraOutputTarget != NULL) {
        ACameraOutputTarget_free(cameraOutputTarget);
        cameraOutputTarget = NULL;
    }

    if (cameraDevice != NULL) {
        camera_status = ACameraDevice_close(cameraDevice);

        if (camera_status != ACAMERA_OK) {
            LOGE("Failed to close CameraDevice.\n");
        }
        cameraDevice = NULL;
    }

    if (sessionOutput != NULL) {
        ACaptureSessionOutput_free(sessionOutput);
        sessionOutput = NULL;
    }

    if (captureSessionOutputContainer != NULL) {
        ACaptureSessionOutputContainer_free(captureSessionOutputContainer);
        captureSessionOutputContainer = NULL;
    }

    LOGI("Close Camera\n");
}

JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_startPreview(JNIEnv *env,
                                                                                     jclass clazz,
                                                                                     jobject surface) {
    openCamera(0, &mainCameraDevice); //Camera 장치 open (c++ 內 사용자 정의 함수)

    mainNativeWindow = ANativeWindow_fromSurface(env, surface); //Activity에서 생성한 surface로 부터 ANative Window 생성

    ACaptureSessionOutput_create(mainNativeWindow, &mainSessionOutput);
    ACaptureSessionOutputContainer_create(&mainCaptureSessionOutputContainer);

    ACaptureSessionOutputContainer_add(mainCaptureSessionOutputContainer, mainSessionOutput);
    ACameraOutputTarget_create(mainNativeWindow, &mainCameraOutputTarget);

    // TEMPLATE_PREVIEW : Create a request suitable for a camera preview window.
    ACameraDevice_createCaptureRequest(mainCameraDevice, TEMPLATE_PREVIEW, &mainCaptureRequest);

    ACaptureRequest_addTarget(mainCaptureRequest, mainCameraOutputTarget);

    //Capture Session 생성
    ACameraDevice_createCaptureSession(mainCameraDevice, mainCaptureSessionOutputContainer, &captureSessionStateCallbacks, &mainCaptureSession);

    //생성된 capture session을 통해 연속해서 이미지 캡쳐 요청
    ACameraCaptureSession_setRepeatingRequest(mainCaptureSession, NULL, 1, &mainCaptureRequest, NULL);

}

JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_stopPreview(JNIEnv *env,
                                                                                    jclass clazz) {
    closeCamera(mainCameraDevice, mainSessionOutput, mainCaptureRequest, mainCameraOutputTarget, mainCaptureSessionOutputContainer);

    //ACameraCaptureSession_stopRepeating(mainCaptureSession);

    //ACaptureSessionOutputContainer_remove(mainCaptureSessionOutputContainer, mainSessionOutput);

    //ACaptureRequest_removeTarget(mainCaptureRequest, mainCameraOutputTarget);

    //ACameraOutputTarget_free(mainCameraOutputTarget);
    //ACaptureSessionOutput_free(mainSessionOutput);
    //ACaptureRequest_free(mainCaptureRequest);

    if (mainNativeWindow != NULL) {
        ANativeWindow_release(mainNativeWindow);
        mainNativeWindow = NULL;
        LOGI("Main view surface is released\n");
    }
}

JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_startExtraView(JNIEnv *env,
                                                                                       jclass clazz,
                                                                                       jobject surface) {

    /* Assuming that camera preview has already been started */
    extraViewWindow = ANativeWindow_fromSurface(env, surface);

    LOGI("Extra view surface is prepared in %p.\n", surface);
    openCamera(1, &extraCameraDevice);
    ACaptureSessionOutputContainer_create(&extraCaptureSessionOutputContainer);

    ACameraDevice_createCaptureRequest(extraCameraDevice, TEMPLATE_STILL_CAPTURE,
                                       &extraCaptureRequest);

    ACameraOutputTarget_create(extraViewWindow, &extraCameraOutputTarget);
    ACaptureRequest_addTarget(extraCaptureRequest, extraCameraOutputTarget);

    ACaptureSessionOutput_create(extraViewWindow, &extraSessionOutput);
    ACaptureSessionOutputContainer_add(extraCaptureSessionOutputContainer,
                                       extraSessionOutput);

    ACameraDevice_createCaptureSession(extraCameraDevice, extraCaptureSessionOutputContainer,
                                       &captureSessionStateCallbacks, &extraCaptureSession);

    ACameraCaptureSession_setRepeatingRequest(extraCaptureSession, NULL, 1, &extraCaptureRequest, NULL);
}

JNIEXPORT void JNICALL Java_org_freedesktop_nativecamera2_NativeCamera2_stopExtraView(JNIEnv *env,
                                                                                      jclass clazz) {
    closeCamera(extraCameraDevice, extraSessionOutput, extraCaptureRequest, extraCameraOutputTarget, extraCaptureSessionOutputContainer);

    //ACameraCaptureSession_stopRepeating(extraCaptureSession);

    //ACaptureSessionOutputContainer_remove(extraCaptureSessionOutputContainer, extraSessionOutput);

    //ACaptureRequest_removeTarget(extraCaptureRequest, extraCameraOutputTarget);

    //ACameraOutputTarget_free(extraCameraOutputTarget);
    //ACaptureSessionOutput_free(extraSessionOutput);
    //ACaptureRequest_free(extraCaptureRequest);

    if (extraViewWindow != NULL) {
        ANativeWindow_release(extraViewWindow);
        extraViewWindow = NULL;
        LOGI("Extra view surface is released\n");
    }
}