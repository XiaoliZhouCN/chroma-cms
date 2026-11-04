@echo off
rem ChromaCMS documentation gate entry point.
rem Local execution policy is AllSigned, so launch with -ExecutionPolicy Bypass.
rem Keep this file ASCII-only: batch files are decoded with the console code page.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-docs.ps1" %*
exit /b %ERRORLEVEL%
