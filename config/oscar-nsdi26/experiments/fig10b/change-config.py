import os
import json
scriptDir = os.path.dirname(os.path.abspath(__file__))
print(scriptDir)
allFiles = os.listdir(scriptDir)
jsonFiles = [f for f in allFiles if f.endswith('.json')]

for jsonFile in jsonFiles:
    with open(f'{scriptDir}/{jsonFile}', 'r') as f:
        config = json.load(f)

    defaultConfig = config["defaultConfig"]
    socketConf = defaultConfig["RoCEv2Socket"]
    socketConf["RetxMode"] = "None"
    defaultConfig["Ipv4GlobalRouting"] = {
        "RandomEcmpRouting": "PerPacket"
    }

    if jsonFile.startswith('oscar'):
        ccConf = config["applicationConfig"][0]["congestionConfig"]["RttRecordStructure"] = "Map"
        

    with open(f"{scriptDir}/{jsonFile}", "w") as f:
        json.dump(config, f, indent=4)
