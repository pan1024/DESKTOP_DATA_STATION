import os, winreg
import time

import requests


def get_equipment_info():
    if os.name != "nt":
        pass
    result = {}
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Software\\FinalWire\\AIDA64\\SensorValues") as key:
        for i in range(winreg.QueryInfoKey(key)[1]):
            name, data, _type = winreg.EnumValue(key, i)
            attr = name.split(".")
            if attr[0] == "Label":
                result[attr[1]] = {}
                result[attr[1]]["label"] = data
            else:
                result[attr[1]]["value"] = data
    return result


def set_params():
    data = get_equipment_info()
    result = {}
    keys = data.keys()
    for i in keys:
        result.update({data.get(i).get("label").replace(" ",""):data.get(i).get("value")})
    return result


if __name__ == "__main__":
    while True:
        try:
            result = requests.post(url="http://192.168.0.253/uploadComputeData", data=set_params())
            while result.status_code == 200:
                result = requests.post(url="http://192.168.0.253/uploadComputeData", data=set_params())
                time.sleep(1)
        except:
            pass

