
# VoidGen: ESP32 TOTP Generator

In this post I'll go through my experience of building a standalone hardware TOTP authenticator using an ESP32. 
# Table of Contents

- [Introduction](#voidgen-esp32-totp-generator)
- [What, why, and how?](#what-why-and-how)
- [Disclaimer](#disclaimer)
- [Hardware](#hardware)
  - [Parts list](#parts-list)
  - [Components](#components)
  - [Wiring](#wiring)
  - [Communication interfaces](#communication-interfaces)
    - [I2C](#i2c)
    - [GPIO](#gpio)
    - [Camera interface](#camera-interface)
- [Software](#software)
  - [Overview](#overview)
  - [User interface](#user-interface)
  - [QR code handling](#qr-code-handling)
  - [Encrypted storage](#encrypted-storage)
  - [TOTP generation](#totp-generation)
- [Demo](#demo)
  - [Adding a new account](#adding-a-new-account)
  - [Generating TOTP codes](#generating-totp-codes)
- [Limitations & improvements](#limitations--improvements)
- [Conclusion](#conclusion)
## What, why, and how?

If you've ever enabled two-factor authentication (2FA) on an online account, you've probably used a Time-based One-Time Password (TOTP). Every 30 seconds, your authenticator app generates a new six-digit code that acts as an additional layer of security when logging in.

Most people use smartphone applications such as Google Authenticator, Microsoft Authenticator, or Authy to generate these codes. Those apps are convenient, but I wanted to explore a different approach: could a small, dedicated piece of hardware perform the same task entirely offline?

That question became the starting point for this project.

The result is **VoidGen**, a standalone TOTP generator built around the ESP32-S3 CAM. The device scans standard TOTP QR codes, securely stores the authentication secrets in encrypted flash memory, and generates valid authentication codes without requiring an internet connection or a smartphone.

The ESP32-S3 CAM serves as the heart of the project. It provides more than enough processing power for QR code decoding, encryption, and TOTP generation, while its integrated OV2640 camera makes account provisioning straightforward by scanning the same QR codes used by common authenticator apps.

To complete the device, I added a small collection of supporting hardware:

-   **SSD1306 OLED display** – displays menus and authentication codes.
-   **DS3231 Real-Time Clock (RTC)** – keeps accurate time even when the ESP32 is powered off.
-   **Rotary encoder with push button** – provides a simple user interface for navigation and PIN entry.

The final result is a compact proof-of-concept hardware authenticator that demonstrates QR code scanning, encrypted local storage, offline TOTP generation, and a simple embedded user interface.


# Disclaimer

This project was created as a proof of concept for a school assignment. While it successfully generates standard TOTP codes and encrypts stored authentication secrets, it omits several security features that would be expected in a production-ready authenticator.

**Do not rely on this device for securing your accounts.** The implementation has not undergone real-world testing and is still under development. By using it you risk losing access to your accounts!

# Hardware



## Parts list

| Component | Purpose |
| --- | --- |
| ESP32-S3 CAM | Main controller and QR code scanner |
| SSD1306 OLED | Displays menus and TOTP codes |
| DS3231 RTC | Provides accurate timekeeping |
| Rotary encoder | User input |



## Components

### ESP-S3 CAM
The ESP32-S3 CAM is the core of the project. It handles all processing, including QR code decoding, TOTP generation, encryption and managing stored accounts. The integrated OV2640 camera makes it possible to scan TOTP setup QR codes without requiring additional hardware.

### SSD1306 OLED display

The OLED display provides a simple interface for the user. It is used to show account names, generated TOTP codes, the remaining validity time and the login screen.

### DS3231 RTC

TOTP codes are time-based, meaning the device needs an accurate clock. The DS3231 RTC module provides reliable timekeeping even when the ESP32 is powered off.

### Rotary encoder

The rotary encoder is used as the primary input method. Rotating it allows navigating between accounts and menus, while the push button is used for selecting options and entering input.

## Wiring

The wiring of the prototype is relatively simple, with most peripherals using standard communication interfaces. The OLED display and RTC communicate using I2C, while the rotary encoder uses GPIO inputs.

_(Insert wiring diagram here)_

The ESP32-S3 CAM board does come with some limitations when choosing GPIO pins. A large portion of the pins are already used internally by the camera module and cannot be freely assigned to other peripherals. Additionally, some pins are reserved for USB communication.

During development, I initially used pins occupied by the camera module, which caused communication issues. Later, I also ran into conflicts with the USB pins and had to switch from the native USB OTG interface to the USB-to-serial (TTL) interface for programming and debugging.

For this project, I decided to use different GPIO pins for the I2C bus instead of the default configuration. The pins were manually defined in software:

```cpp
#define I2C_SDA 42
#define I2C_SCL 41

```

## Communication interfaces

The project uses several different communication methods depending on the requirements of each component. Some peripherals use standard communication protocols such as I2C, while others rely on direct GPIO signals.


| Component | Interface |
| --- | --- |
| SSD1306 OLED display | I2C |
| DS3231 RTC | I2C |
| Rotary encoder | GPIO |
| OV2640 camera | Parallel camera interface |

## I2C

The OLED display and RTC module communicate with the ESP32 using the I2C (Inter-Integrated Circuit) protocol.

I2C is a synchronous serial communication protocol that uses two signal lines:

-   **SDA (Serial Data)** — used for transferring data between devices
    
-   **SCL (Serial Clock)** — provides the clock signal used to synchronize communication
    

In this project, the ESP32 acts as the I2C master, while the OLED display and RTC module act as slave devices. Both peripherals share the same SDA and SCL lines, with each device being identified by its own address.

This allows multiple devices to be connected to the same bus while only requiring two signal wires.

The I2C bus is initialized in software by specifying the GPIO pins defined earlier in the program for SDA and SCL:

```cpp
Wire.begin(I2C_SDA, I2C_SCL);
```

## GPIO

GPIO (General Purpose Input/Output) pins are used for simple digital signals where a dedicated communication protocol is not required.

The rotary encoder uses GPIO inputs to communicate user input to the ESP32. The encoder contains two switches that generate two digital signals, commonly referred to as channel A and channel B. These signals are offset by 90 degrees (quadrature encoding), which allows the software to determine both the direction and amount of rotation.

For example, when rotating the encoder clockwise, channel A changes state before channel B. When rotating it in the opposite direction, the order is reversed. By detecting these changes, the ESP32 can determine whether the user is increasing or decreasing a value.

The push button on the encoder is also connected directly to a GPIO pin. Unlike the rotational input, the button only provides a simple HIGH/LOW signal and is used for selecting options and confirming input.

![Rotary encoder quadrature signal example](image-link)

## Camera interface

The OV2640 camera uses a parallel camera interface to transfer image data to the ESP32-S3.

Unlike serial protocols such as I2C, a parallel camera interface transfers multiple bits of data at the same time using several dedicated signals. These include the image data lines, pixel clock, and synchronization signals required to correctly interpret each frame.

Because the camera requires a large number of GPIO pins, the available pins for connecting additional peripherals are reduced. This affected the placement of the other components, especially the I2C bus used by the OLED display and RTC module.

During development, some initial GPIO selections conflicted with pins already assigned to the camera interface. After adjusting the wiring and changing the pin definitions in software, all peripherals were able to operate correctly together.

# Software

## Overview

The software handles user input, QR code scanning, encrypted storage of account secrets and generation of TOTP codes.

The project is written in C++ in Arduino IDE using the Arduino framework for the ESP32. 

The software can be divided into four main parts:
- **User interface**
Handles the OLED display and rotary encoder input. This includes the login screen, account selection and switching between menus.
- **QR code handling**
Handles scanning and decoding TOTP setup QR codes using the camera module. The scanned data is then parsed to extract the account name and secret key.
- **Encrypted storage**
Manages storing and loading account data from flash memory. Account secrets are encrypted using AES-256-GCM before being written to storage.
- **TOTP generation**
Uses the stored secret and current time from the RTC module to generate time-based authentication codes.

## User interface

The user interface consists of two main menus: the search menu and the add menu.

The search menu is used for viewing stored accounts and generating TOTP codes. Each entry displays the associated service name, the current six-digit authentication code, and the remaining validity time. The remaining time is visualized using an updating progress bar, allowing the user to easily see when the code will refresh.

The add menu is used for adding new accounts to the device. Switching to this menu automatically activates the camera and starts the QR code scanning process. Once a valid TOTP setup QR code is detected and successfully decoded, the account information is stored and the user is automatically returned to the search menu.

Navigation between the two menus is handled using the rotary encoder. Rotating the encoder changes the selected account, while a long button press switches between the search and add menus.

The interface is implemented using a state machine, where each menu and screen is represented as a separate state. This allows user input and display updates to be handled depending on the current mode of operation.

## QR code handling

The QR code scanning functionality is handled using a dedicated QR code decoding library. The camera processing runs as a separate task on the ESP32's second core, allowing QR code scanning to happen independently from the rest of the application. This prevents the camera processing from blocking other functionality, such as updating the user interface or handling user input.

To reduce the amount of data that needs to be processed, the camera is configured to capture grayscale images at QVGA resolution. High-resolution images are unnecessary for QR code detection, and using a lower resolution reduces the number of pixels that need to be processed, improving performance and reducing processing time.

The camera is initialized during startup using a custom configuration function. This function defines the GPIO pins used by the OV2640 camera interface, as the required pins depend on the specific ESP32-S3 CAM board being used.

While the add menu is active, the software continuously searches for a QR code. Once a valid TOTP setup QR code is detected and successfully decoded, the resulting data payload is passed to the parsing and storage functions. These functions extract the required account information and securely store the generated account entry.

## Encrypted storage

The account information is stored using a custom data structure containing the required information for each TOTP account. Before being written to the ESP32 filesystem using LittleFS, the data is encrypted using AES-256-GCM.

AES (Advanced Encryption Standard) is a symmetric encryption algorithm, meaning the same key is used for both encryption and decryption. The "256" refers to the size of the encryption key, while GCM (Galois/Counter Mode) is a mode of operation that provides both data encryption and integrity verification.

Using AES-GCM prevents stored authentication secrets from being directly readable and also protects against unnoticed modification of the stored data. When the encrypted data is loaded, the authentication tag is verified. If the file has been modified, the verification fails and the stored information is rejected.

The encryption key used for protecting account data is stored separately in the ESP32's Non-Volatile Storage (NVS) using the Preferences library.


## TOTP generation

When a new account is added, the QR code payload is parsed to extract the required TOTP information. The label is used as the service name displayed in the user interface, while the secret key is stored and later used for generating authentication codes. Other parameters contained in the QR payload are currently ignored.

The device uses a TOTP library to handle the code generation process. This produces standard six-digit authentication codes that remain valid for 30 seconds, following the standard TOTP format used by common authenticator applications.

Since TOTP codes are time-based, accurate timekeeping is essential. The current time is obtained from the DS3231 RTC module and is used together with the stored secret key to generate the correct authentication code.

The remaining validity time of the current code is also calculated from the current timestamp. This value is used by the user interface to display the progress bar showing when the current code will expire.

# Demo
Below are videos that show the device in operation
## Adding a new account
[gif]

## Generating TOTP codes
[gif]



# Limitations & improvements

Although the device successfully demonstrates offline TOTP generation, there are several areas that could be improved before it could be considered a production-ready authenticator.

The current authentication and key management system is one of the main security limitations. The encryption master key and user PIN are stored in the ESP32 NVS storage, meaning that an attacker with physical access and the ability to read the device memory could potentially extract this information. A future version could use a hardware-based authentication method, such as a fingerprint reader, combined with a more secure key storage approach. The fingerprint would be used to authenticate the user and unlock access to the encryption key.

The current OLED display is also a limitation. The 32-pixel height makes displaying larger amounts of information difficult. A future version could use a more common 128x64 OLED display or an e-ink display to improve readability and reduce power consumption.

Currently, the device relies on USB power, which limits portability. Adding battery power would allow the authenticator to be used independently from a computer. However, this would introduce additional challenges such as battery charging, power regulation, and reducing power consumption to extend battery life.




# Conclusion

This project was a fun and challenging experience that combined multiple areas of interest, including embedded systems, hardware communication, cryptography, and software development.

The final result is a standalone offline TOTP authenticator that combines QR code scanning, encrypted local storage, real-time clock integration, and a simple user interface into a single device. While the current implementation is still a proof of concept and has limitations that prevent it from being used as a production-ready authenticator, it successfully achieves the core functionality required for a dedicated hardware-based TOTP generator.

I am looking forward to continuing to improve the project, exploring better security solutions, and learning more through future iterations. I would love to hear any feedback, suggestions, or ideas for improving the project!
