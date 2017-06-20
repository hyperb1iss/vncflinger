package org.chemlab;

interface IVNCService {
    boolean start();
    boolean stop();

    boolean setPort(int port);
    boolean setV4Address(String addr);
    boolean setV6Address(String addr);

    boolean setPassword(String password);
    boolean clearPassword();
}
