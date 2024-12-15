#include <stdio.h>

#include <stdlib.h>

#include <pthread.h>

#include <semaphore.h>

#include <unistd.h>

#include <signal.h>

#include <sys/wait.h>

#include <stdbool.h>

#include <mqueue.h>

#include <string.h>

// MACROS
#define MQ_NAME "/mq_cola" // Nombre de la cola de mensajes
#define MQ_NAME2 "/mq_cola2" // Nombre de la cola de mensajes
#define SEM_NAME "/semaforo" // Nombre del semáforo
#define ARR_SIZE 128

struct mq_attr attributes = {
  .mq_flags = 0, // flags de la cola (ignorados por mq_open())
  .mq_maxmsg = 10, // Número máximo de mensajes en la cola
  .mq_curmsgs = 0, // Número actual de mensajes en la cola (ignorados por mq_open())
  .mq_msgsize = sizeof(char) * ARR_SIZE // Tamano máximo de cada mensaje
};

// VARIABLES GLOBALES
mqd_t mqd; // Cola de mensajes
mqd_t mqd_b; // Cola de mensajes


sem_t sem_exp;
sem_t sem_diag; // Semáforo para el diagnóstico
sem_t sem_farm; // Semáforo para la farmacia
sem_t * sem_rec; // Semáforo para la farmacia


int pacientes_dados_de_alta = 0; // Contador de pacientes dados de alta
int nuevos_pacientes = 0; // Contador de nuevos pacientes

pthread_t exp_thr, farm_thr, diag_thr; // Identificadores de los hilos

pid_t pid_hospital, pid_recepcion; // Identificadores de los procesos padre e hijo

// FUNCIONES

// Función para generar un número aleatorio entre un rango
int tiempo_aleatorio(int min, int max) {
  return rand() % (max - min + 1) + min;
}

// Función para manejar la senal SIGINT (interrupción)
void sigint(int sig) {
	 
  mq_close(mqd); // Cierra la cola de mensajes
  mq_close(mqd_b); // Cierra la cola de mensajes

  mq_unlink(MQ_NAME); // Elimina la cola de mensajes
  mq_unlink(MQ_NAME2); // Elimina la cola de mensajes

  sem_destroy( &sem_farm); // Destruye el semáforo de farmacia
  sem_destroy( &sem_diag); // Destruye el semáforo de diagnóstico
  sem_destroy( sem_rec); // Destruye el semáforo de diagnóstico
  
  sem_unlink(SEM_NAME); // Elimina el semáforo
  printf("El padre ha terminado\n");
  
}

// Función para finalizar el proceso hijo
void endProcess(int sig) {
  printf("El hijo ha terminado\n");
  exit(0);
}

void endHospital(int sig) {
  pthread_cancel(exp_thr);
  pthread_cancel(diag_thr);
  pthread_cancel(farm_thr);

  
  // El padre espera que los hilos finalicen
  pthread_join(exp_thr, NULL);
  pthread_join(diag_thr, NULL);
  pthread_join(farm_thr, NULL);

  printf("El hijo ha terminado\n");
  exit(0);
}

void trat(int sig){
	signal(SIGUSR2, trat);
}


// Función para notificar a la recepción (envía la senal SIGUSR1)
void notificaRecepcion(int sig) {
  signal(SIGUSR1, notificaRecepcion); // Recibe senales SIGUSR1
  kill(pid_recepcion, SIGUSR1); // Envía la senal SIGUSR1 al proceso de recepción
}

// Función para actualizar el número de pacientes dados de alta
void pacientesConMedicacion(int sig) {
  signal(SIGUSR1, pacientesConMedicacion); // Espera para la senal de nuevo
  pacientes_dados_de_alta += 1; // Incrementa el contador de pacientes dados de alta
  printf("Pacientes dados de alta: %d\n", pacientes_dados_de_alta);
}

//----------------------------------------------------------------

// Función del hilo de exploración
void * exploracion(void * args) {
  printf("[Exploracion] Comienzo mi ejecucion...\n");
  // Abre la cola de mensajes en modo solo lectura
  mqd_t mqd = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0644, &attributes);
  mqd_b = mq_open(MQ_NAME2, O_CREAT | O_WRONLY, 0644, &attributes);
  
  while (1) {
    char paciente[ARR_SIZE];
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    printf("[Exploracion] Esperando a un paciente...\n");
    	
    mq_receive(mqd, paciente, sizeof(char) * ARR_SIZE, NULL); // Recibe un mensaje de la cola
    printf("[Exploracion] Recibido paciente: %s. Realizando exploracion...\n", paciente);
    
    // Simula el tiempo de exploración
    sleep(tiempo_aleatorio(1, 3));
    sem_post(sem_rec);

    printf("[Exploracion] Exploracion completa con paciente %s. Notificando diagnostico...\n", paciente);

    mq_send(mqd_b, paciente, sizeof(char) * ARR_SIZE, 1);
      	
  }
}

// Función del hilo de diagnóstico
void * diagnostico(void * args) {
  printf("[Diagnostico] Comienzo mi ejecucion...\n");
  

  mqd_t mqd_b = mq_open(MQ_NAME2, O_CREAT | O_RDONLY, 0644, &attributes);
  char paciente[ARR_SIZE];

  while (1) {

    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
     
    sem_wait( &sem_diag); // Decrementa el valor del semáforo (espera hasta que se libere)
    mq_receive(mqd_b, paciente, sizeof(char) * ARR_SIZE, NULL); // Recibe un mensaje de la cola
    
    printf("[Diagnostico] Realizando pruebas diagnosticas...\n");
    sleep(tiempo_aleatorio(5, 10)); // Simula el tiempo de las pruebas de diagnóstico

    printf("[Diagnostico] Diagnostico completado. Notificando farmacia...\n");

    // Desbloquea el semáforo de farmacia para continuar el proceso en farmacia
    pthread_kill(farm_thr, SIGUSR2);
    sem_post( &sem_diag);
  }
}

// Función del hilo de farmacia
void * farmacia(void * args) {
  
  printf("[Farmacia] Comienzo mi ejecucion...\n");
  signal(SIGUSR2,trat);
  while (1) {
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    sem_wait( &sem_farm); // Decrementa el semáforo, esperando que el diagnóstico esté listo
    
    pause();
    printf("[Farmacia] Preparando medicacion...\n");
    sleep(tiempo_aleatorio(1, 3)); // Simula el tiempo de preparación de la medicación

    printf("[Farmacia] Medicacion lista. Enviando señal de alta...\n");

    // Notifica a la recepción de que la medicación está lista
    kill(pid_recepcion, SIGUSR1);
    sem_post( &sem_farm);
  }
}

void main(int argv, char * argc[]) {
  sem_init( &sem_diag, 0, 1); // Inicializa el semáforo para diagnóstico con valor 0 (bloqueado)
  sem_init( &sem_farm, 0, 1); // Inicializa el semáforo para farmacia con valor 0 (bloqueado)
  sem_rec = sem_open( SEM_NAME , O_CREAT , 0666, 1);

  pid_recepcion = fork(); // Crea un proceso hijo para la recepción

  if (pid_recepcion != 0) { // Proceso padre
    pid_hospital = fork(); // Crea un proceso hijo para el hospital
    if (pid_hospital != 0) { // Proceso abuelo (padre de hospital)
      int status;
      signal(SIGINT, sigint); // Configura la señal SIGINT (Ctrl+C) para manejarla
      for (int i = 0; i < 2; i++) { // El padre espera a sus dos hijos (hospital y recepción)
        pid_t child_pid = wait( &status); // Espera a que los hijos terminen
      }
    } else {
      // Proceso hospital
      signal(SIGINT, endHospital); // Maneja la señal SIGINT
      signal(SIGUSR1, notificaRecepcion); // Configura la señal SIGUSR1 para notificar a la recepción
      printf("[Hospital] Comienzo mi ejecucion...\n");

      // Crea los hilos para exploración, diagnóstico y farmacia
      int exp = pthread_create( &exp_thr, NULL, exploracion, NULL);
      if (exp != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", exp);
        exit(EXIT_FAILURE);
      }

      int diag = pthread_create( &diag_thr, NULL, diagnostico, NULL);
      if (diag != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", diag);
        exit(EXIT_FAILURE);
      }

      int farm = pthread_create( &farm_thr, NULL, farmacia, NULL);
      if (farm != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", farm);
        exit(EXIT_FAILURE);
      }

      // El padre espera que los hilos finalicen
      pthread_join(exp_thr, NULL);
      pthread_join(diag_thr, NULL);
      pthread_join(farm_thr, NULL);
    }
  } else {
    // Proceso recepción
    printf("[Recepcion] Comienzo mi ejecucion...\n");
    signal(SIGINT, endProcess); // Maneja la señal SIGINT
    signal(SIGUSR1, pacientesConMedicacion); // Configura la señal SIGUSR1 para contar pacientes dados de alta

    while (1) {
      char paciente[ARR_SIZE];
      sem_wait( sem_rec);
      sleep(tiempo_aleatorio(1, 10)); // Simula el tiempo de registro de nuevos pacientes
      sprintf(paciente, "El mensaje de la recepcion: %d", nuevos_pacientes += 1);
      printf("[Recepcion] Registrando nuevo paciente: %s...\n", paciente);

      // Abre la cola de mensajes en modo solo escritura
      mqd = mq_open(MQ_NAME, O_WRONLY | O_CREAT, 0644, &attributes); //El propietario del archivo puede leer y escribir en la cola de mensajes y el grupo y los otros usuarios solo pueden leer la cola de mensajes.

      // Envía el mensaje de registro a la cola
      mq_send(mqd, paciente, sizeof(char) * ARR_SIZE, 1);
          

    }
  }

  exit(0); // Finaliza el proceso
}
