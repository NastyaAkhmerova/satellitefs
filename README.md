# SatelliteFS

Виртуальная файловая система уровня ядра Linux,
моделирующая искусственные спутники планеты Марс.

## Сборка

```bash
cd src
make
```

## Загрузка модуля

```bash
sudo insmod satellitefs.ko
mkdir -p /mnt/satellitefs
sudo mount -t satellitefs none /mnt/satellitefs
```

## Использование

```bash
cat /mnt/satellitefs/README
echo "45 0.8 10" > /mnt/satellitefs/ARES-1
cat /mnt/satellitefs/ARES-1.state
```

## Выгрузка

```bash
sudo umount /mnt/satellitefs
sudo rmmod satellitefs
```
