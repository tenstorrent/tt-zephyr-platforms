# Changelog

All notable changes to the spirom proto files will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 0.1.0 - 15/08/2024

- First addition of spirom protobufs.
- Added 3 different protobuf files:
  - fw_table.proto : Values that can be modified by flashing the board
  - flash_info.proto: Meta data from the flashing process
  - read_only.proto: values that are not expected to change by flashing