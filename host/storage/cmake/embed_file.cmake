# 把任意文件嵌入成 C 字节数组初始化片段（供 #include 使用）。
#
# 用法：
#   cmake -DINPUT=<源文件> -DOUTPUT=<输出 .inc> -P embed_file.cmake
#
# 采用「每个字节写成 (char)0xNN,」的形式，理由：
#   - 对二进制、引号、反斜杠、非 ASCII 都安全，不需要按语言猜转义规则；
#   - 不用字符串字面量，因此不受 ISO C 的 4095 字符最小实现限制
#     （-Wpedantic 的 -Woverlength-strings 会把它变成构建失败）；
#   - 显式 (char) 转换让 0x80..0xff 的取值不依赖 char 的符号性。
#
# 输出是若干行 "(char)0xNN," 项；调用方负责加数组声明、结尾 NUL 与分号。

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "embed_file.cmake requires -DINPUT=<file> -DOUTPUT=<file>")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "embed_file.cmake: input file not found: ${INPUT}")
endif()

file(READ "${INPUT}" embed_hex HEX)
string(LENGTH "${embed_hex}" embed_hex_len)
if(embed_hex_len EQUAL 0)
    message(FATAL_ERROR "embed_file.cmake: input file is empty: ${INPUT}")
endif()

# 一次处理 64 个十六进制字符 = 32 字节，避免逐字节循环在 CMake 里过慢。
set(embed_chunk_hex 64)
set(embed_body "")
set(embed_offset 0)
while(embed_offset LESS embed_hex_len)
    math(EXPR embed_remaining "${embed_hex_len} - ${embed_offset}")
    if(embed_remaining LESS embed_chunk_hex)
        set(embed_take ${embed_remaining})
    else()
        set(embed_take ${embed_chunk_hex})
    endif()
    string(SUBSTRING "${embed_hex}" ${embed_offset} ${embed_take} embed_chunk)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "(char)0x\\1, " embed_chunk "${embed_chunk}")
    string(APPEND embed_body "    ${embed_chunk}\n")
    math(EXPR embed_offset "${embed_offset} + ${embed_chunk_hex}")
endwhile()

file(WRITE "${OUTPUT}" "${embed_body}")
