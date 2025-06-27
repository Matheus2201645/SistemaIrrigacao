#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#define LED_PIN 20 //LED indicador de nivel
#define BUTTON_PIN 16 //Botao rega manual
#define NIVEL_PIN 28 //Sensor de nivel do reservatorio

const bool DESATIVARNIVEL = 0;// para testes

uint64_t ultimaRega = 0;
#define ON_ANGLE 180
#define OFF_ANGLE 0
#define SERVO 27
#define SERVO_180 2500
#define SERVO_0 500



#define TEMPOULTIMAREGA 5000000 //tempo minimo permitido entre regas em us

const float LIMITE_PERCENTUAL = 0.02; //Regar quando tiver menos que 2% de umidade valor maximo de 10%
const int LIMITE_UMIDADE = (int)(-9562.6*LIMITE_PERCENTUAL+1870.6);//Formula obtida a partir da umidade do solo

volatile uint32_t DELAY_MOV=1*1000000; //tempo para o servo atingir a posicao final em us
#define VOLUME 50 //Quantidade de agua escolhida pelo usuario
volatile uint32_t DELAYREGAR=VOLUME*84700+1887500; // Formula obtida a partir da vazao  em us

void goServo(int servo, int ang){ //Posiciona o servo no angulo escolhido
  pwm_set_enabled(pwm_gpio_to_slice_num(SERVO),true); //"ativa" o motor
  int duty = (((float)(SERVO_180 - SERVO_0) / 180.0) * ang) + SERVO_0; // calcula o duty cycle nescessario
  pwm_set_gpio_level(servo, duty); //
  busy_wait_us(DELAY_MOV);
  pwm_set_enabled(pwm_gpio_to_slice_num(SERVO),false);  //Desativa o motor para reduzir o consumo
  
}

void regar(){
  if((gpio_get(NIVEL_PIN)==0) || (DESATIVARNIVEL)){ //verifica se existe agua suficiente
    uint64_t tempoPassado=time_us_64()-ultimaRega;
    if(tempoPassado>TEMPOULTIMAREGA){ //verifica se passou o tempo minimo entre regas
      goServo(SERVO,ON_ANGLE); //abre a valvula
      busy_wait_us(DELAYREGAR);
      goServo(SERVO,OFF_ANGLE); //fecha a valvula
      ultimaRega=time_us_64(); //atualiza o tempo da ultima rega
    }
  }
  if(gpio_get(NIVEL_PIN)==0){ //Atualiza o indicador de nivel
      gpio_put(LED_PIN,0);
    }else{
      gpio_put(LED_PIN,1);
    }
}

void gpioHandler(uint gpio,uint32_t events){ //Determina qual funcao chamar a partir da interrupcao de gpio
  if(gpio==BUTTON_PIN){
    regar();

  }else if(gpio==NIVEL_PIN){ 
    if(events == GPIO_IRQ_EDGE_FALL){ //Quando detectar a borda de descida do reservatorio(Vazio->Cheio) desliga o LED
        gpio_put(LED_PIN,0); 

    }else if(events == GPIO_IRQ_EDGE_RISE){ //Quando detectar a borda de subida do reservatorio(Cheio->Vazio) Liga o LED
        gpio_put(LED_PIN,1);
    }
  }
}




bool timer_cb(repeating_timer_t *t) { //Atualiza o indicador de nivel e mede a umidade do solo a cada 30s
    if(gpio_get(NIVEL_PIN)==0){ //Nivel de agua alto
    gpio_put(LED_PIN,0); 
    }else{
    gpio_put(LED_PIN,1); //Nivel de agua baixo
    }
    uint16_t raw = adc_read();
    printf("%u,%.3f\n", raw);  // Exibe a umidade medido no monitor serial
    if(raw>LIMITE_UMIDADE){ //Se umidade menor que o limite determinado rega
      regar();
    }
     return true; // Repetir indefinidamente
}

int main()
{
    stdio_init_all(); //inicia a comunicacao serial
    uint32_t clk = 125000000; //clock do RP2040
    uint32_t div = clk/(20000*50);
    ultimaRega = time_us_64();


    // Setup medicao umidade
    adc_init();
    adc_gpio_init(26);                 // GPIO26 = ADC0
    adc_select_input(0);

    // Setup GPIOs
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN,GPIO_IN);

    gpio_init(NIVEL_PIN);
    gpio_set_dir(NIVEL_PIN,GPIO_IN);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN,GPIO_OUT);

    //Setup do PWM para controle do Servo
    gpio_init(SERVO);
    gpio_set_function(SERVO,GPIO_FUNC_PWM);
    pwm_set_gpio_level(SERVO,0);
    uint slice_num =pwm_gpio_to_slice_num(SERVO);
    pwm_config config =pwm_get_default_config();
    pwm_config_set_clkdiv(&config,(float)div);
    pwm_config_set_wrap(&config,20000);
    pwm_init(slice_num,&config,false);  
    pwm_set_enabled(slice_num,false);

    //interrupcao do botao de rega manual
    gpio_set_irq_enabled_with_callback(BUTTON_PIN,GPIO_IRQ_EDGE_FALL,1,&gpioHandler);
    
    //Interrupcao do nivel de agua
    gpio_set_irq_enabled_with_callback(NIVEL_PIN,GPIO_IRQ_EDGE_FALL,1,&gpioHandler);
    //gpio_set_irq_enabled_with_callback(NIVEL_PIN,GPIO_IRQ_EDGE_RISE,1,&gpioHandler);

    // Setup do timer de medicao da umidade
    repeating_timer_t timer;
    add_repeating_timer_ms(30000, timer_cb, NULL, &timer);

    if(gpio_get(NIVEL_PIN)==0){ //Inicia indicador de nivel
      gpio_put(LED_PIN,0);
    }else{
      gpio_put(LED_PIN,1);
    }

    while (true) { //Espera por interrupcoes em um estado de menor consumo
      __wfi();
    }
}
