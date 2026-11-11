# NGPrinter

[![Build & Release](https://github.com/OWNER/ngprinter/actions/workflows/build.yml/badge.svg)](https://github.com/OWNER/ngprinter/actions/workflows/build.yml)

NGPrinter is a PDF pinter server. It can print http/https URLs pdf files. It uses the Qt framework for cross-platform support.

> 注：上方徽章里的 `OWNER` 请替换为你的 GitHub 用户名/组织名。

## 支持平台

- Windows xp/7/8/10/11
- Linux
- Mac OS

## 构建

### 本地构建 (Windows, Qt 5.6.3 + MinGW 4.9 32 位)

直接运行仓库根目录的脚本：

```bat
build.bat
```

产物位于 `build/` 目录。

### GitHub Actions 自动构建 / 发布

仓库已内置 [`.github/workflows/build.yml`](.github/workflows/build.yml)：

- 推送到 `master` / `vs` 分支或发起 PR：自动编译并上传构建产物 (artifact)。
- 推送 `v` 开头的 tag（如 `v1.0.0`）：自动编译并创建 GitHub Release，附带打包好的 zip。

```bash
git tag v1.0.0
git push origin v1.0.0
```

## 使用

### 获取系统打印机列表

**请求地址:**  http://localhost:8765/printers

**请求方式:**  GET

**返回格式:**  json

**返回示例:**

```json
{
    "code": 0,
    "msg": "success",
    "data": [
        {
            "name": "HP LaserJet Professional P2055dn",
            "status": "online",
            "isDefault": true
        },
        {
            "name": "Microsoft Print to PDF",
            "status": "online",
            "isDefault": false
        }
    ]
}
```

### 打印PDF文件

**请求地址:**  http://localhost:8765/print

**请求方式:**  GET

**请求参数:**

| 参数名  | 必选 | 类型   | 说明                           |
| :------ | :--- | :----- | :----------------------------- |
| url     | 是   | string | PDF文件URL                     |
| printer | 否   | string | 打印机名称, 不传则为默认打印机 |
| copies  | 否   | int    | 打印份数, 默认为1              |
| fit     | 否   | int    | 默认为实际尺寸打印，传 1 时等比适合可打印区域 |

**返回格式:**  json

**返回示例:**

成功：

```json
{
    "code": 0,
    "msg": "success",
    "data": null
}
```

失败（如 url 缺失、下载失败、打印机不存在等）：

```json
{
    "code": 1,
    "msg": "Printer not found: XXX",
    "data": null
}
```
