/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
otDeviceRole mock_role;
otCommissionerState mock_commissioner;
otError mock_dataset_error, mock_pending_error, mock_pending_result;
otOperationalDataset mock_dataset;
static otDatasetMgmtSetCallback pending_callback;
static void *pending_context;
static otReceiveDiagnosticGetCallback diagnostic_callback;
static void *diagnostic_context;
void mock_reset(void) {
    mock_role=OT_DEVICE_ROLE_DISABLED;
    mock_commissioner=OT_COMMISSIONER_STATE_DISABLED;
    mock_dataset_error=OT_ERROR_NONE;
    mock_pending_error=OT_ERROR_NOT_FOUND;
    mock_pending_result=OT_ERROR_NONE;
    memset(&mock_dataset,0,sizeof(mock_dataset));
    mock_dataset.mComponents.mIsNetworkNamePresent=true;
    strcpy(mock_dataset.mNetworkName.m8,"test-mesh");
    pending_callback=NULL;
    diagnostic_callback=NULL;
    mock_time=0;
}
void mock_complete_pending(void) {
    if(pending_callback) {otDatasetMgmtSetCallback callback=pending_callback;pending_callback=NULL;callback(mock_pending_result,pending_context);}
}
void mock_deliver_diagnostic(uintptr_t token) {
    otMessageInfo info={0};
    if(diagnostic_callback) {diagnostic_callback(OT_ERROR_NONE,(otMessage *)(uintptr_t)1,&info,(void *)(token?token:(uintptr_t)diagnostic_context));}
}
