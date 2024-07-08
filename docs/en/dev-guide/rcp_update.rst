***************************
2.4. RCP Update Mechanism
***************************

The ESP Thread Border Router SDK supports a feature for updating RCP firmware from the Host SoC. This process can be automated by enabling the ``AUTO_UPDATE_RCP`` option in the menuconfig.

Additionally, a CLI command (``otrcp update``) is available to promptly initiate the RCP update process. The command will be supported by enabling ``OPENTHREAD_RCP_COMMAND`` option.

2.4.1. RCP Image
----------------

One or two RCP images are stored in a configurable SPIFFS partition under a customizable prefix.

When utilizing OTA firmware for updating the RCP image, the image will be saved in the directory ``/rcp_fw/ot_rcp_idx/``, with ``idx`` representing the variable ``rcp_update_seq`` passed during the invocation of the ``download_ota_image`` function.

2.4.2. RCP Update Rules
-----------------------

2.4.2.1 RCP sequence number and RCP verified flag
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The RCP sequence number and RCP verified flag are crucial variables for updating the RCP image. They determine the current image index based on the following rules:

- If RCP verified flag is true, then current image index = RCP sequence number
- If RCP verified flag is false, then current image index = 1 - RCP sequence number

2.4.2.2 RCP image update process
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The RCP update process proceeds as follows:

- Retrieve the RCP sequence number and RCP verified flag from the NVS. If not available, generate default values (RCP sequence number = 0, RCP verified flag = 1).
- Calculate the current image index ``idx``.
- Read the RCP image from the path ``/rcp_fw/ot_rcp_idx/`` and transfer it to the RCP device via the serial port for updating.
- If the update is successful, set the RCP verified flag to true and store it in the NVS. Otherwise, set it to false and store it in the NVS.


2.4.3 The RCP Image Automatically Update and Rollback Mechanism
---------------------------------------------------------------

When Automatically updating the RCP, the RCP image with the current image index will be downloaded. After the reboot, the Border Router will detect the status of the RCP. If the RCP fails to boot, the backup image will be flashed to the RCP to revert the change.

When downloading the OTA image from the server, the current RCP image will be marked as the backup image via modifying the RCP sequence number and RCP verified flag, and the previous backup image will be overridden. After the download completes, the current image index will be updated.


2.4.4. Update RCP image via extension command 
---------------------------------------------

To utilize the extension command for updating the RCP image, ensure that the macro ``OPENTHREAD_RCP_COMMAND`` is enabled.

After starting the border router, update the RCP using the following command:

.. code-block:: bash

     otrcp update