#include "ids.h"

pthread_cond_t algorithm_signal = PTHREAD_COND_INITIALIZER;
pthread_mutex_t algorithm_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
IDS ids;

void* ids_thread_f(void *x){

    while(true){
        ids.frame_loop();
    }
    return NULL;
}

void IDS::init(){
    initialize_camera();
    setting_auto_params();
    change_params();
    create_auto_trackbars();
    update_autoparams();
//    create_manual_trackbars();
//    update_params();
    pthread_mutex_init(&algorithm_signal_mutex,NULL);

    int x = 0;
    if (pthread_create(&frame_thread,NULL,ids_thread_f,&x)){
        std::cout<<"Error creating frame-grabbing thread"<<std::endl;
    }
    else{
        std::cout << "Frame-grabbing thread started" << std::endl;
    }
}
void IDS::initialize_camera() {
    m_hCamera = (HIDS)0;
    pthread_mutex_init(&frame_mutex,NULL);
    pthread_mutex_init(&signal_mutex,NULL);

    ZeroMemory (m_Images, sizeof(m_Images));
    INT nRet = is_InitCamera(&m_hCamera, NULL);
    if (nRet == IS_SUCCESS) {
        std::cout << "Camera initialized!" << std::endl;
    }

    // Setting the pixels clock
    UINT nPixelClockDefault = 25;
    nRet = is_PixelClock(m_hCamera, IS_PIXELCLOCK_CMD_SET, (void*)&PixelClock, sizeof(PixelClock));

    if (nRet == IS_SUCCESS) {
        std::cout << "Camera pixel clock succes" << std::endl;
    }
    else if (nRet == IS_NOT_SUPPORTED) {
        std::cout << "Camera pixel clock setting is not supported" << std::endl;
    }

    // Set the color mode
    INT colorMode = IS_CM_BGR8_PACKED;
    nRet = is_SetColorMode(m_hCamera, colorMode);

    if (nRet == IS_SUCCESS) {
        std::cout << "Camera color mode succes" << std::endl;
    }

    INT displayMode = IS_SET_DM_DIB;
    nRet = is_SetDisplayMode(m_hCamera, displayMode);

    IS_RECT rectAOI;

    rectAOI.s32X     = (752 - IDS_WIDTH)/2;

    rectAOI.s32Y     = (480 - IDS_HEIGHT);

    rectAOI.s32Width = IDS_WIDTH;

    rectAOI.s32Height = IDS_HEIGHT;



    nRet = is_AOI( m_hCamera, IS_AOI_IMAGE_SET_AOI, (void*)&rectAOI, sizeof(rectAOI));

    //    int retInt = is_SetAllocatedImageMem(m_hCamera, 752, 480, 24, (char*)ids_frame.ptr(), &memID);
    //    int retInt = is_AllocImageMem(m_hCamera, 752, 480, 24, &pMem, &memID);
    //    if (retInt != IS_SUCCESS){
    //        std::cout << "Error in allocating memory" << std::endl;
    //    }
    //    if (is_SetImageMem(m_hCamera, pMem, memID) == IS_SUCCESS)
    //        {
    //        is_CaptureVideo(m_hCamera, IS_WAIT);
    //        }
    //    else
    //    {
    //        std::cout << "Error in activating memory" << std::endl;
    //    }
    if(_AllocImages(IDS_WIDTH, IDS_HEIGHT, 24)){
        std::cout << "Buffer alloc success" << std::endl;
        if(is_CaptureVideo(m_hCamera, IS_WAIT) == IS_SUCCESS){
            std::cout << "Capture Enabled" << std::endl;
        } else  {
            std::cout << "Capture Fail" << std::endl;
        }

        if(is_EnableEvent (m_hCamera, IS_SET_EVENT_FRAME) == IS_SUCCESS)
        {
            std::cout << "Event Enabled" << std::endl;
//            cvNamedWindow("frame window",1);
        } else {
            std::cout << "Event Fail" << std::endl;
        }
    } else {
        std::cout << "Error in allocating memory" << std::endl;
    }
    is_EnableAutoExit(m_hCamera, IS_ENABLE_AUTO_EXIT);
}
// Capture a frame from IDS
void IDS::setAlgorithmReady(){
    algorithm_ready = true;
}

void IDS::frameEvent(){
    INT ret = is_WaitEvent(m_hCamera, IS_SET_EVENT_FRAME, INFINITE);
    if(ret == IS_SUCCESS){
        pthread_mutex_lock(&signal_mutex);
        pthread_cond_signal(&frame_signal);
        pthread_mutex_unlock(&signal_mutex);
    }
}
void IDS::frame_loop()
{
    INT ret = is_WaitEvent(m_hCamera, IS_SET_EVENT_FRAME, 1000);
    if(ret == IS_SUCCESS){
        is_DisableEvent (m_hCamera, IS_SET_EVENT_FRAME);
        ProcessFrame();
        is_EnableEvent (m_hCamera, IS_SET_EVENT_FRAME);
    }
    if(ret == IS_TIMED_OUT){
         std::cout << "Event Timeout" << std::endl;
    }
}
void IDS::ProcessFrame ()
{
    INT dummy = 0;
    bool bDraw = true;
    double fps;
    char *pLast = NULL, *pMem = NULL;
    is_GetActSeqBuf (m_hCamera, &dummy, &pMem, &pLast);
    m_pLastBuffer = pLast;
    //    nReceived++;
    //    UpdateCounters ();

    if (algorithm_ready)
    {

        pthread_mutex_lock(&frame_mutex);
        if (is_GetFramesPerSecond (m_hCamera, &fps) == IS_SUCCESS)
            updateFps (fps);
        pthread_mutex_unlock(&frame_mutex);

//        algorithm_ready = false;
        if (m_pLastBuffer)
        {
            static int cnt = 0;
            int nNum;
            INT ret = IS_SUCCESS;
            nNum = _GetImageNum (m_pLastBuffer);
            INT nId = _GetImageID(m_pLastBuffer);

            pthread_mutex_lock(&frame_mutex);
            ret = is_LockSeqBuf (m_hCamera, nNum, m_pLastBuffer);
            INT copy = is_CopyImageMem(m_hCamera, m_pLastBuffer, nId, (char*)ids_frame.ptr());
            ret |= is_UnlockSeqBuf (m_hCamera, nNum, m_pLastBuffer);
            pthread_mutex_unlock(&frame_mutex);

            pthread_mutex_lock(&algorithm_signal_mutex);
            pthread_cond_signal(&algorithm_signal);
            pthread_mutex_unlock(&algorithm_signal_mutex);
        }
    }
}
void IDS::get_frame_to(cv::Mat &output) {
    pthread_cond_wait(&algorithm_signal, &algorithm_signal_mutex);

    pthread_mutex_lock(&frame_mutex);
    ids_frame.copyTo(output);
    pthread_mutex_unlock(&frame_mutex);
}
HIDS IDS::getCameraHID(){
    return m_hCamera;
}
INT IDS::_GetImageID (char* pbuf)
{
    if (!pbuf)
        return 0;

    for (unsigned int i = 0; i < sizeof(m_Images) / sizeof(m_Images[0]); i++)
        if (m_Images[i].pBuf == pbuf)
            return m_Images[i].nImageID;

    return 0;
}

INT IDS::_GetImageNum (char* pbuf)
{
    if (!pbuf)
        return 0;

    for (unsigned int i = 0; i < sizeof(m_Images) / sizeof(m_Images[0]); i++)
        if (m_Images[i].pBuf == pbuf)
            return m_Images[i].nImageSeqNum;

    return 0;
}
bool IDS::_AllocImages (int nWidth, int nHeight, int nBitspp)
{
    m_pLastBuffer = NULL;

    UINT nAbsPosX;
    UINT nAbsPosY;

    is_AOI(m_hCamera, IS_AOI_IMAGE_GET_POS_X_ABS, (void*)&nAbsPosX , sizeof(nAbsPosX));
    is_AOI(m_hCamera, IS_AOI_IMAGE_GET_POS_Y_ABS, (void*)&nAbsPosY , sizeof(nAbsPosY));

    is_ClearSequence (m_hCamera);
    for (unsigned int i = 0; i < sizeof(m_Images) / sizeof(m_Images[0]); i++)
    {
        if (m_Images[i].pBuf)
        {
            is_FreeImageMem (m_hCamera, m_Images[i].pBuf, m_Images[i].nImageID);
            m_Images[i].pBuf = NULL;
            m_Images[i].nImageID = 0;
        }

        if (is_AllocImageMem (m_hCamera, nWidth, nHeight, nBitspp, &m_Images[i].pBuf,
                              &m_Images[i].nImageID) != IS_SUCCESS)
            return FALSE;
        if (is_AddToSequence (m_hCamera, m_Images[i].pBuf, m_Images[i].nImageID) != IS_SUCCESS)
            return FALSE;

        m_Images[i].nImageSeqNum = i + 1;
        m_Images[i].nBufferSize = nWidth * nHeight * nBitspp / 8;
    }

    return TRUE;
}
void IDS::_FreeImages ()
{
    m_pLastBuffer = NULL;
    //printf ("freeing image buffers\n");
    for (unsigned int i = 0; i < sizeof(m_Images) / sizeof(m_Images[0]); i++)
    {
        if (m_Images[i].pBuf)
        {
            is_FreeImageMem (m_hCamera, m_Images[i].pBuf, m_Images[i].nImageID);
            m_Images[i].pBuf = NULL;
            m_Images[i].nImageID = 0;
        }
    }
}
void IDS::_EmptyImages ()
{
    INT ret = IS_SUCCESS;
    m_pLastBuffer = NULL;
    for (unsigned int i = 0; i < sizeof(m_Images) / sizeof(m_Images[0]); i++)
    {
        if (m_Images[i].pBuf)
        {
            ret = is_LockSeqBuf (m_hCamera, m_Images[i].nImageSeqNum, m_Images[i].pBuf);
            ZeroMemory (m_Images[i].pBuf, m_Images[i].nBufferSize);
            ret |= is_UnlockSeqBuf (m_hCamera, m_Images[i].nImageSeqNum, m_Images[i].pBuf);
        }
    }
}
void IDS::updateFps (double fps)
{
    if (meanfps == 0.0)
        meanfps = fps;
    else
        meanfps = (meanfps * 15 + fps) / 16;
}
double IDS::getFPS(){
    return meanfps;
}
void IDS::exit(){
    _FreeImages ();
    is_ExitCamera (m_hCamera);
}
//Updating parameters from trackbars in while loop
void IDS::update_params() {
    PixelClock = (UINT)pixelclock_slider;
    is_PixelClock(m_hCamera, IS_PIXELCLOCK_CMD_SET, (void*)&PixelClock, sizeof(PixelClock));

    Exposure = (double)(exposure_slider/30.);
//    is_Exposure(m_hCamera, IS_EXPOSURE_CMD_SET_EXPOSURE, (void*)&Exposure, sizeof(Exposure));

    FPS = (double)fps_slider;
    is_SetFrameRate(m_hCamera, FPS, &NEWFPS);

    is_SetHWGainFactor(m_hCamera, IS_SET_MASTER_GAIN_FACTOR, Master_GAIN_Factor);
    is_SetHWGainFactor(m_hCamera, IS_SET_GREEN_GAIN_FACTOR, Green_GAIN_Factor);
    is_SetHWGainFactor(m_hCamera, IS_SET_BLUE_GAIN_FACTOR, Blue_GAIN_Factor);
    is_SetHWGainFactor(m_hCamera, IS_SET_RED_GAIN_FACTOR, Red_GAIN_Factor);
    Sharpness = (INT)sharpness_slider;
    is_EdgeEnhancement(m_hCamera, IS_EDGE_ENHANCEMENT_CMD_SET, &Sharpness, sizeof(Sharpness));
    is_Gamma(m_hCamera, IS_GAMMA_CMD_SET, &Gamma, sizeof(Gamma));
}

//Enabling auto parameters
void IDS::setting_auto_params() {
    double enable = 1;
    double disable = 0;

    double autobalance_speed = 50.;
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_SENSOR_WHITEBALANCE, &enable, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_WHITEBALANCE, &enable, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_AUTO_WB_SPEED, &autobalance_speed, 0);

    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_FRAMERATE, &disable, 0);

    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_GAIN, &disable, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_SENSOR_GAIN, &disable, 0);

    double exposure_speed = 50.;
//    is_SetAutoParameter(m_hCamera, IS_SET_AUTO_SPEED, &exposure_speed, 0);
//    is_SetAutoParameter(m_hCamera, IS_SET_AUTO_SHUTTER_MAX, &max_exposure, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_SHUTTER, &disable, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_SENSOR_SHUTTER, &disable, 0);
    is_SetAutoParameter(m_hCamera, IS_SET_ENABLE_AUTO_SENSOR_GAIN_SHUTTER, &disable, 0);

    /* Receive configuration */

    nSizeOfParam = sizeof(AES_CONFIGURATION) - sizeof(CHAR) + sizeof(AES_PEAK_CONFIGURATION);
    CHAR *pBuffer = new char[nSizeOfParam];
    memset(pBuffer, 0, nSizeOfParam);

    pAesConfiguration = (AES_CONFIGURATION*)pBuffer;
    pAesConfiguration->nMode = IS_AES_MODE_PEAK;
    pPeakConfiguration = (AES_PEAK_CONFIGURATION*)pAesConfiguration->pConfiguration;

    INT nRet = 0;
    nRet = is_AutoParameter(m_hCamera, IS_AES_CMD_GET_CONFIGURATION_DEFAULT, pAesConfiguration, nSizeOfParam);
    pAesConfiguration->nMode = IS_AES_MODE_PEAK;
    min_exposure = min_exposure_slider/100.0;
    max_exposure = max_exposure_slider/100.0;
    pPeakConfiguration->f64Minimum = min_exposure;
    pPeakConfiguration->f64Maximum = max_exposure;
    pPeakConfiguration->nGranularity = IS_AES_GRANULARITY_PER_10000;
    pPeakConfiguration->nHysteresis = 1;
    pPeakConfiguration->nReference = 15;

    IS_RECT autoAOI;
    uint16_t autoHeight = 200;
    autoAOI.s32X     = (752 - IDS_WIDTH)/2;
    autoAOI.s32Y     = (480 - IDS_HEIGHT);
    autoAOI.s32Width = IDS_WIDTH;
    autoAOI.s32Height = autoHeight;

    pPeakConfiguration->rectUserAOI = autoAOI;

    /* set configuration */
    nRet = is_AutoParameter(m_hCamera, IS_AES_CMD_SET_CONFIGURATION, pAesConfiguration , nSizeOfParam);

    /* Enable */
    INT nEnable = IS_AUTOPARAMETER_ENABLE;
    INT nType = IS_AES_MODE_PEAK;
    is_AutoParameter(m_hCamera, IS_AES_CMD_SET_TYPE, &nType, sizeof(nType));
    is_AutoParameter(m_hCamera, IS_AES_CMD_SET_ENABLE, &nEnable, sizeof(nEnable));
    if(nRet)
        std::cout << "Autoparams problem" << std::endl;
}

//Changing camera setting and gettign default variables
void IDS::change_params() {
//    is_Exposure(m_hCamera, IS_EXPOSURE_CMD_SET_EXPOSURE, (void*)&Exposure, sizeof(Exposure));

    is_SetFrameRate(m_hCamera, FPS, &NEWFPS);

    // Enable setting gain boost
//    int nRet = is_SetGainBoost(m_hCamera, IS_SET_GAINBOOST_ON);
//    if (nRet == IS_SUCCESS) {
//        std::cout << "Enabling Gain success" << std::endl;
//    }
    //Get gain factors
    Master_GAIN_Factor = is_SetHWGainFactor(m_hCamera, IS_GET_DEFAULT_MASTER_GAIN_FACTOR,100);
    Red_GAIN_Factor = is_SetHWGainFactor(m_hCamera, IS_GET_DEFAULT_RED_GAIN_FACTOR, 100);
    Green_GAIN_Factor = is_SetHWGainFactor(m_hCamera, IS_GET_DEFAULT_GREEN_GAIN_FACTOR, 100);
    Blue_GAIN_Factor = is_SetHWGainFactor(m_hCamera, IS_GET_DEFAULT_BLUE_GAIN_FACTOR, 100);

    int nRet = is_EdgeEnhancement(m_hCamera, IS_EDGE_ENHANCEMENT_CMD_GET_DEFAULT, &Sharpness, sizeof(Sharpness));
    if (nRet == IS_SUCCESS) {
        std::cout << "Edge enhancement default success" << std::endl;
    }
    sharpness_slider = (UINT)Sharpness;

//    nRet = is_Gamma(m_hCamera, IS_GAMMA_CMD_GET_DEFAULT, &Gamma, sizeof(Gamma));
//    if (nRet == IS_SUCCESS) {
//        std::cout << "Gamma default success" << std::endl;
//    }

}
void IDS::update_autoparams() {

    is_Gamma(m_hCamera, IS_GAMMA_CMD_SET, &Gamma, sizeof(Gamma));

    min_exposure = min_exposure_slider/100.0;
    max_exposure = max_exposure_slider/100.0;
    pPeakConfiguration->f64Minimum = min_exposure;
    pPeakConfiguration->f64Maximum = max_exposure;
    pPeakConfiguration->nGranularity = IS_AES_GRANULARITY_PER_10000;
    pPeakConfiguration->nHysteresis = (UINT)hysteresis_slider;
    pPeakConfiguration->nReference = (UINT)reference_slider;

    /* set configuration */
    is_AutoParameter(m_hCamera, IS_AES_CMD_SET_CONFIGURATION, pAesConfiguration , nSizeOfParam);

    /* Enable */
    INT nEnable = IS_AUTOPARAMETER_ENABLE;
    INT nType = IS_AES_MODE_PEAK;
    is_AutoParameter(m_hCamera, IS_AES_CMD_SET_TYPE, &nType, sizeof(nType));
    is_AutoParameter(m_hCamera, IS_AES_CMD_SET_ENABLE, &nEnable, sizeof(nEnable));
}

static void update_suwaki(int,void*){
    ids.update_params();
}
static void update_auto_trackbars(int,void*){
    ids.update_autoparams();

}
//Creating in debug mode trackbars
void IDS::create_manual_trackbars(void){
    cvNamedWindow("ids", 1);
    cv::createTrackbar("Pixel", "ids", &pixelclock_slider, 40, update_suwaki);
    cv::createTrackbar("Exposure", "ids", &exposure_slider, 30*30, update_suwaki);
    cv::createTrackbar("FPS", "ids", &fps_slider, 100, update_suwaki);
    cv::createTrackbar("Master", "ids", &Master_GAIN_Factor, 300, update_suwaki);
    cv::setTrackbarMin("Master", "ids", 100);
    cv::createTrackbar("Green", "ids", &Green_GAIN_Factor, 300, update_suwaki);
    cv::setTrackbarMin("Green", "ids",100);
    cv::createTrackbar("Red", "ids", &Red_GAIN_Factor, 300, update_suwaki);
    cv::setTrackbarMin("Red", "ids", 100);
    cv::createTrackbar("Blue", "ids", &Blue_GAIN_Factor, 300, update_suwaki);
    cv::setTrackbarMin("Blue", "ids", 100);
    cv::createTrackbar("Sharpness", "ids", &sharpness_slider, 9, update_suwaki);
    cv::setTrackbarMin("Sharpness", "ids", 0);
    cv::createTrackbar("Gamma", "ids", &Gamma, 300, update_suwaki);
}
void IDS::create_auto_trackbars(void){
    cvNamedWindow("IDS Settings", 1);
    cv::createTrackbar("Min Exposure", "IDS Settings", &min_exposure_slider, 5*100, update_auto_trackbars);
    cv::createTrackbar("Max Exposure", "IDS Settings", &max_exposure_slider, 30*100, update_auto_trackbars);
    cv::createTrackbar("Reference", "IDS Settings", &reference_slider, 2000, update_auto_trackbars);
    cv::createTrackbar("Hysteresis", "IDS Settings", &hysteresis_slider, 500, update_auto_trackbars);
    cv::createTrackbar("Gamma", "IDS Settings", &Gamma, 400, update_auto_trackbars);
}
