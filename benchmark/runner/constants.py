from pathlib import Path

# Implementation -> receiver binary name (basename)
IMPL_BIN_NAME = {
    "uring": "uring_receiver",
    "asio": "asio_receiver",
    "asio_uring": "asio_uring_receiver",
    "asio_ioctx_mt": "asio_ioctx_mt_receiver",
    "asio_uring_ioctx_mt": "asio_uring_ioctx_mt_receiver",
    "bsd": "bsd_receiver",
}

CLIENT_BIN_NAME = "client"
