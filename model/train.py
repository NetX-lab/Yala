import sys
import numpy as np
import pandas as pd
from sklearn.ensemble import GradientBoostingRegressor
from sklearn.linear_model import LinearRegression
import pickle

# Train per-resource model for memory subsystem contention
# nf: network function
#
# df: dataframe of training data.
# 
# model_name: the model to train
# - linear: linear regression
# - slomo: slomo mem model (gradient boosting regression)
# - yala: yala mem model (gradient boosting regression)
#
# model_path: path to store the trained models
def train_mem(nf, df, model_name="linear", model_path="models.pkl"):
    try:
        models = pickle.load(open(model_path, 'rb'))
    except:
        models = {}

    # models
    if model_name == "linear":
        model = LinearRegression()
        x_train = df[['ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']]
    elif model_name == "slomo":
        model = GradientBoostingRegressor()
        x_train = df[['ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']]
    elif model_name == "yala_mem":
        model = GradientBoostingRegressor()
        x_train = df[['pktsize','flowsize','ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']]
    else:
        print("Invalid model name")
        return
    y_train = df['perf']  

    if nf not in models.keys():
        models[nf] = {}

    if model_name not in models[nf]:
        print(f"Training {model_name} model for {nf} ...")
        model.fit(x_train, y_train)
        models[nf][model_name] = model
    
    print("Dumping models ...")
    pickle.dump(models, open(model_path, 'wb'))

    return 1


# Train per-resource model for regex accelerator contention
def train_reg(nf, df, model_path="models.pkl"):
    try:
        models = pickle.load(open(model_path, 'rb'))
    except:
        models = {}

    if "yala_reg" not in models[nf]:       
        print(f"Training yala_reg model for {nf} ...")          
        T0 = LinearRegression()
        # fit solo performance of regex part
        T0.fit(df[['mtbr']],df['perf_reverse'])
        models[nf]["yala_reg"] = T0
        
    print("Dumping models ...")
    pickle.dump(models, open(model_path, 'wb'))
    

if __name__ == "__main__":
    nf = "flowmon" 
    path = f"../profile/{nf}/sample_slomo_train"
    df = pd.read_csv(path, names=['flowsize','pktsize','ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss', 'perf'], sep=' ')
    print(f"Total {str(len(df))} group(s) of data for training linear/slomo")
    train_mem(nf, df, model_name="linear", model_path="models.pkl")
    train_mem(nf, df, model_name="slomo", model_path="models.pkl")

    path = f"../profile/{nf}/sample_yala_mem_train"
    df = pd.read_csv(path, names=['flowsize','pktsize','ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss', 'perf'], sep=' ')
    print(f"Total {str(len(df))} group(s) of data for training per-resource model regarding memory subsystem contention in yala")
    train_mem(nf, df, model_name="yala_mem", model_path="models.pkl")
    
    path = f"../profile/{nf}/sample_yala_reg_train"
    df = pd.read_csv(path, names=['mtbr', 'perf_reverse'], sep=' ')
    print(f"Total {str(len(df))} group(s) of data for training per-resource model regarding regex accelerator contention in yala")
    train_reg(nf, df, model_path="models.pkl")
