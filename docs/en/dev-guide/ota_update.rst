***************************
2.3. OTA Update Mechanism
***************************

The ESP Thread Border Router supports building the RCP image into the Border Router image. Upon boot the RCP image will be automatically downloaded to the RCP if the Border Router detects the RCP chip failure to boot.

Hardware Prerequisites
-----------------------

To perform OTA update, the following devices are required:

- An ESP Thread Border Router
- A Linux Host machine

In addition to the UART connection to the RCP chip, two extra GPIO pins are required to control the RESET and the BOOT pin of the RCP. For ESP32-H2 the BOOT pin is GPIO8.

Download and Update the Border Router Firmware
-----------------------------------------------

Create a HTTPS server providing the OTA image file, excute the follow command on your Linux Host machine:

.. code-block:: bash

    openssl s_server -WWW -key ca_key.pem -cert ca_cert.pem -port 8070

The private key and the certificate shall be accpetable for the Border Router. If they are self-signed, make sure to add the public key to the trusted key set of the Border Router.

Now the image can be downloaded on the Border Router:

.. code-block:: bash

    ota download https://${HOST_URL}:8070/ota_with_rcp_image

- Tips 1: For optimizing the firmware of border router, `CONFIG_COMPILER_OPTIMIZATION_SIZE` and `CONFIG_NEWLIB_NANO_FORMAT` are enabled by default.
- Tips 2: If the OTA function is enabled, it is recommended to optimize the ot_rcp firmware size before building the OTA image. Please refer to `ot_rcp README <https://github.com/espressif/esp-idf/blob/master/examples/openthread/ot_rcp/README.md>`_ for detailed steps.

After downloading the Border Router will reboot and update itself with the new firmware. The RCP will also be updated if the firmware version changes.

The OTA Image File Structure
-----------------------------

The OTA image is a single file containing the meta data of the RCP image, the RCP bootloader, the RCP partition table and the firmwares.

The image can be generated with script :component_file:`esp_rcp_update/create_ota_image.py`. By default this file is called automatically during build and packs the `ot_rcp` example image into the Border Router firmware.

The RCP image header is defined as the following diagram:

    +---------------+----------------+---------------+
    |     0xff      |  Header size   |       0       |
    +---------------+----------------+---------------+
    |  File Type 0  |  File size     |  File offset  |
    +---------------+----------------+---------------+
    |  File Type 1  |  File size     |  File offset  |
    +---------------+----------------+---------------+

    ...


 The file type is defined as the following table:

 .. list-table::
   :widths: 25 50

   * - 0
     - RCP version
   * - 1
     - RCP flash arguments
   * - 2
     - RCP bootloader
   * - 3
     - RCP partition table
   * - 4
     - RCP firmware
   * - 5
     - Border Router firmware
