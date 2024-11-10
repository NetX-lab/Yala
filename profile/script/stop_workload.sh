ssh -l root dpu "2>/dev/null sudo pkill click"
ssh -l root dpu " 2>/dev/null sudo kill \$(pidof doca\_flow\_pipeline)" 
ssh -l root dpu " 2>/dev/null sudo kill \$(pidof ip\_pipeline)" 
sleep 1