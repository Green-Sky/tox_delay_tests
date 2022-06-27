set datafile separator ','
#set xdata time
set key autotitle columnhead

set terminal wxt

set y2tics # enable second axis
set ytics nomirror # dont show the tics on that side

#plot "test1_delays_1656344547_tcp.csv" using 1:3 with points pointtype 0, '' using 1:5 with lines axis x1y2
plot "test1_delays_1656344547_tcp.csv" using 1:3 with line linetype 1, '' using 1:5 with lines axis x1y2

