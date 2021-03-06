[Unit]
Description=Gromox zcore service
Documentation=man:zcore(8gx)
PartOf=gromox-exch.target
After=mariadb.service mysql.service

[Service]
Type=simple
ExecStart=@libexecdir@/gromox/zcore

[Install]
WantedBy=multi-user.target
