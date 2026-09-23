package io.github.cepeter.telegramhider;

interface ICatalogService {
    void submit(int account, in long[] ids, in String[] titles);
    void reportStatus(String hook, String status, String detail);
}
