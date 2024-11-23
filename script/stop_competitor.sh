ssh -l root dpu "2>/dev/null sudo pkill click1"
ssh -l root dpu " 2>/dev/null sudo kill \$(pidof doca\_flow\_pipeline1)" 
ssh -l root dpu " 2>/dev/null sudo kill \$(pidof ip\_pipeline1)"
ssh -l root dpu " 2>/dev/null sudo pkill mbw" 
ssh -l root dpu " 2>/dev/null sudo pkill accbench" 
sleep 1