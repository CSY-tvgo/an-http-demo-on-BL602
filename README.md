## 介绍  

这个仓库里存了几个博流 [BL-IoT-SDK](https://github.com/bouffalolab/bl_iot_sdk) 的使用示例，可供参考。  

## 使用方法  
下载 [BL-IoT-SDK](https://github.com/bouffalolab/bl_iot_sdk)，把本仓库内的项目的文件夹放入 `bl_iot_sdk/customer_app/get-start/`，然后可阅读 [BL-IoT-SDK Docs » 快速入门](https://bouffalolab.github.io/bl_iot_sdk/get-started/index_602.html) 中描述的方法来开始编译和烧写。  

## 本仓库包含的例子  
+ ### [httpGETbilibili_test](./httpGETbilibili_test/)  

  用 BL602 发出 HTTP 请求的例子。  

  详细内容请阅读：https://verimake.com/d/36  

+ ### [positive_dashboard](./positive_dashboard/)

  用 BL604 做一个疫情数据显示板，可以每隔一段时间从互联网上下载江浙皖部分城市及周边城市当前 COVID-19 确诊人数，然后显示在电子墨水屏上。  

  包含：联网发出 HTTP 请求、擦 / 写 / 读 Flash、通过 SPI 控制电子墨水屏等。  

  详细内容请阅读：https://verimake.com/d/183  

另：BL602 / BL604 均可使用 BL-IoT-SDK，如果您想让代码运行在与示例中不同的开发板上，只需修改引脚序号等少量代码即可。  
