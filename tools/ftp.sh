#!/bin/sh

###############################################################################
## Copyright(C) 2014-2024 Qiware technology Co., Ltd
##
## 功    能: 用于从指定FTP服务器取指定的文件
## 注意事项: 
##		无
## 作    者: # Qifeng.zou # 2014.09.17 #
###############################################################################

## 参数列表
## ./ftp.sh 127.0.0.1 qifeng 123456 abc.dat
## 参数$1: IP地址
## 参数$2: 用户名
## 参数$3: 登陆密码
## 参数$4: 文件路径

ftp -i -in <<!
open $1
user $2 $3
bin
passive
get $4
bye
!
