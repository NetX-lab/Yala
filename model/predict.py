import sys
import numpy as np
import pandas as pd
from tabulate import tabulate
from sklearn.ensemble import GradientBoostingRegressor
from sklearn.linear_model import LinearRegression
from sklearn.metrics import mean_absolute_percentage_error 
import warnings
import pickle

# execution-pattern-based composition
from compose_by_pattern import compose_by_pattern
from compose_by_pattern import linear_compose
from compose_by_pattern import no_compose
from compose_by_pattern import min_compose

warnings.filterwarnings('ignore')


def PMACC(Y_actual, Y_predicted, ratio=0.1, verbose=False):
    cnt = 0
    for i ,j in zip(Y_actual, Y_predicted):
        if verbose:
            print("%.4f %f %f" % ((np.abs(i-j)/i),i,j))
        if np.abs(i-j)/i <= ratio:
            cnt = cnt + 1
    return 100*(cnt/len(Y_actual))




def predict(nf, df, model_name="models.pkl",verbose=False):
    models = {}
    try:
        models = pickle.load(open(model_name, 'rb'))
    except:
        print(f"Model: {model_name} does not exist")

    y_test = df['perf']
    linear = models[nf]["linear"]
    slomo = models[nf]['slomo']
    tomur_mem = models[nf]['tomur_mem']
    tomur_reg = models[nf]["tomur_reg"]
    
    y_pred_lr = linear.predict(df[['ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']])
    y_pred_slomo = slomo.predict(df[['ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']]) # Predicted values    
    # # For SLOMO, extrapolate
    # # We provide the extrapolation implementation of SLOMO for reference. Please collected corresponding data to enable this function.
    # flowsz0 = 16384
    # pktsize0 = 82
    # solo_perf_old = parse_perf(f"../../Exp_data/profile/{nf}/pc/target_perf_{flowsz0}_{pktsize0}")
    # wss_old = parse_pc(f"../../Exp_data/profile/{nf}/pc/comp_pc_{flowsz0}_{pktsize0}",single="wss")
    # df_copy = df.copy()
    # extrapolate_rows = df_copy.loc[(df_copy["flowsize"] >= 16384*0.8) & (df_copy["flowsize"] <= 16384*1.2)]
    # for index, row in extrapolate_rows.iterrows():
    #     flowsz = df_copy.at[index, "flowsize"]
    #     pktsz = df_copy.at[index, "pktsize"]
    #     df_copy.at[index, 'l2crd'] = df_copy.at[index,"l2crd"] * (parse_pc(f"../../Exp_data/profile/{nf}/pc/comp_pc_{flowsz}_{pktsz}",single="wss")/wss_old)
    #     df_copy.at[index, 'l2cwr'] = df_copy.at[index,"l2cwr"] * (parse_pc(f"../../Exp_data/profile/{nf}/pc/comp_pc_{flowsz}_{pktsz}",single="wss")/wss_old)
    #     df_copy.at[index, 'wss'] = df_copy.at[index, 'wss'] + (parse_pc(f"../../Exp_data/profile/{nf}/pc/comp_pc_{flowsz}_{pktsz}",single="wss") - wss_old)
    # for index, row in extrapolate_rows.iterrows():
    #     y_pred_slomo[index] = y_pred_slomo[index] + parse_perf(f"../../Exp_data/profile/{nf}/pc/target_perf_{flowsz}_{pktsz}") - solo_perf_old

    y_pred_tomur = []
    y_pred_tomur_mem = tomur_mem.predict(df[['pktsize','flowsize','ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss']])
    y_pred_tomur_regex = []
    self_inverse = tomur_reg.predict(df[["mtbr"]])   
    inverse_sum = df["inverse_sum"]
    for t0,t1 in zip(self_inverse,inverse_sum): 
        result = 1/(t0+t1)
        y_pred_tomur_regex.append(result)
    
    for y_mem,y_regex in zip(y_pred_tomur_mem,y_pred_tomur_regex):
        perf = compose_by_pattern([1,1], [y_mem,y_regex], [1,0])
        y_pred_tomur.append(perf)


    mape = {}
    stdev={}
    pm5= {}
    pm10= {}
    mape['linear'] = 100*mean_absolute_percentage_error(y_test, y_pred_lr)
    mape['slomo'] = 100*mean_absolute_percentage_error(y_test, y_pred_slomo)
    mape['regex'] = 100*mean_absolute_percentage_error(y_test, y_pred_tomur_regex)
    mape['tomur'] = 100*mean_absolute_percentage_error(y_test, y_pred_tomur)


    pm5['linear'] = PMACC(y_test, y_pred_lr,ratio=0.05)
    pm5['slomo'] = PMACC(y_test, y_pred_slomo,ratio=0.05)
    pm5['regex'] = PMACC(y_test, y_pred_tomur_regex,ratio=0.05)
    pm5['tomur'] = PMACC(y_test, y_pred_tomur,ratio=0.05)
    
    pm10['linear'] = PMACC(y_test, y_pred_lr, ratio=0.1)
    pm10['slomo'] = PMACC(y_test, y_pred_slomo, ratio=0.1)
    pm10['regex'] = PMACC(y_test, y_pred_tomur_regex, ratio=0.1)
    pm10['tomur'] = PMACC(y_test, y_pred_tomur, ratio=0.1)    
    

    print(f"Model: {model_name}")
    print(f"NF: {nf}")
    print(f"Test set size: {len(y_test)}")
    # Create a sample DataFrame with large numbers
    results = pd.DataFrame([
        {'Model': 'Linear','MAPE(%)': f"{mape['linear']:1.3f}", '+/-5% Acc(%)': f"{pm5['linear']:1.3f}", '+/-10% Acc(%)': f"{pm10['linear']:1.3f}"},
        {'Model': 'SLOMO','MAPE(%)': f"{mape['slomo']:1.3f}", '+/-5% Acc(%)': f"{pm5['slomo']:1.3f}", '+/-10% Acc(%)': f"{pm10['slomo']:1.3f}"},
        {'Model': 'Regex','MAPE(%)': f"{mape['regex']:1.3f}", '+/-5% Acc(%)': f"{pm5['regex']:1.3f}", '+/-10% Acc(%)': f"{pm10['regex']:1.3f}"},
        {'Model': 'Tomur','MAPE(%)': f"{mape['tomur']:1.3f}", '+/-5% Acc(%)': f"{pm5['tomur']:1.3f}", '+/-10% Acc(%)': f"{pm10['tomur']:1.3f}"}
    ])

    print(tabulate(results, headers='keys', tablefmt='psql', showindex=False, numalign="left"))

    if verbose:
        err_all = []
        for i, j in zip(y_test, y_pred_tomur):
            print(np.abs(i-j)/i)
   
    return 1


if __name__ == "__main__":
    nf = "flowmon"
    test_data_path = f"../profile/{nf}/sample_test"         
    df_test = pd.read_csv(test_data_path, names=['mtbr','inverse_sum','flowsize','pktsize','ipc', 'irt', 'l2crd', 'l2cwr', 'memrd', 'memwr', 'wss', 'perf'], sep=' ')
    
    predict(nf, df_test,model_name="models.pkl")
        
