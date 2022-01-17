#include <iostream>

int main()
{
      int dev_id = -1;
    int open_count = 15;
    while (true){
        dev_id = device_open(dev.c_str());
        if(dev_id <0){
            log_error("open dev error \n");
            sleep(1);
            open_count --;
            if(open_count < 0) {
                log_error("open dev [%s] timeout",dev.c_str());
                return -1;
            }
        }
        else{
            break;
        }
    }
    char tmp = 0x20;
    log_info("open successful [%s][%d]",dev.c_str(),dev_id);
    // sleep(1);

    SenderX sender(dev_id);
    sender.flush();
    int rec_cont = 256;
    log_info("get time delay...");
    while (rec_cont){
        char re = sender.get_byte();
        log_info("[%02x]",re&0xff);
        if( re >= 0x30 ||re <= 0x39){
            break;
        }
        rec_cont --;
    }
    if(rec_cont < 1){
        log_error("no rec delay time...");
        device_close(dev_id);
        return -2;
    }
    sender.crc_flg = true;
    sender.send_byte(tmp);

    tmp = 0x20;
    sender.send_byte(tmp);

    usleep(2000*1000);

    log_info("start send file...");
    sender.flush();
    int count = 0;
    rec_cont = 256;
    while (1){
        char re = sender.get_byte();
        if(re == TIME_OUT){
            count ++ ;
            if(count > 2)
                break;
        }
        rec_cont -- ;
    }
    if(rec_cont < 1){
        log_error("no rec timeout error...");
        device_close(dev_id);
        return -3;
    }
    log_info("start...");
    sender.flush();
    sender.send_file(file.c_str());
    device_close(dev_id);
    return (sender.result == "Done") ? 0 : -4 ;
}

