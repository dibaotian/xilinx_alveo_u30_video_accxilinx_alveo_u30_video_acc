   # it is based on the xilinx ALVEO U30 and  video-sdk 1.5/ video-sdk2.0, please deploy the sdk in advance
   
   # Build the application
   # 1 source /opt/xilinx/xcdr/setting.sh
   # 2 build the codec library
      cd libsrc
      make
   # 3 build app
       cd app
       make
   # the exe file will be in the app folder.
