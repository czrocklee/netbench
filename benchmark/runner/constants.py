from pathlib import Path

# Implementation -> receiver binary name (basename)
RECEIVER_BIN_NAME = {
    "bsd": "bsd_receiver",
    "uring": "uring_receiver",
    "asio": "asio_receiver",
    "asio_uring": "asio_uring_receiver",
    "asio_ioctx_mt": "asio_ioctx_mt_receiver",
    "asio_uring_ioctx_mt": "asio_uring_ioctx_mt_receiver",
}

CLIENT_BIN_NAME = "client"

# Implementation -> pingpong binary name (basename)
PINGPONG_BIN_NAME = {
    "bsd": "bsd_pingpong",
    "uring": "uring_pingpong",
    "uring_sqpoll": "uring_pingpong",
    "uring_sqpoll_zc": "uring_pingpong",
    "asio": "asio_pingpong",
    "asio_uring": "asio_uring_pingpong",
}
