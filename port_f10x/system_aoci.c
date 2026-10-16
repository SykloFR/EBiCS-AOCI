/*
 * system_aoci.c : horloge du contrôleur AOCI (APM32F103RC, quartz 12 MHz)
 *
 * Remplace system_gd32f10x.c (qui suppose un quartz 8 MHz et 108 MHz).
 * Reprend la séquence du firmware AOCI d'origine (FUN_0800a100 / FUN_0800a174,
 * voir aoci-firmware-re/brochage.md) :
 *   - nominal : quartz 12 MHz / 2 x 11 = 66 MHz ; AHB 66 MHz, APB2 66 MHz, APB1 33 MHz ;
 *   - repli si le quartz ne démarre pas : HSI/2 x 16 = 64 MHz ; APB1 32 MHz ;
 *   - flash : 2 états d'attente, prélecture activée.
 * Contrairement à la version GD, aucune boucle infinie : si le quartz ou la PLL
 * ne répondent pas, on bascule sur le repli HSI. Le firmware doit alors
 * s'appuyer sur SystemCoreClock (et non sur une constante) pour ses temps.
 *
 * Licence : BSD-3 (code propre au portage, structure inspirée du CMSIS).
 */

#include "gd32f10x.h"

#ifndef AOCI_HXTAL_TIMEOUT
#define AOCI_HXTAL_TIMEOUT   3000U   /* tours d'attente du quartz (origine : 3000) ; surchargeable pour tester le repli */
#endif
#ifndef AOCI_PLL_TIMEOUT
#define AOCI_PLL_TIMEOUT     1000U   /* tours d'attente de la PLL (origine : 1000) */
#endif
#define AOCI_FMC_WS_PFEN     BIT(4)  /* prelecture flash (FLASH_ACR = 0x32 relevee sur le firmware d'origine : 2 etats d'attente + prelecture) */
#define AOCI_VECT_TAB_OFFSET 0x5000U /* appli à 0x08005000, après le bootloader AOCI */

#if (HXTAL_VALUE != 12000000U)
#error "Le quartz du contrôleur AOCI est de 12 MHz : compiler avec -DHXTAL_VALUE=12000000U"
#endif

uint32_t SystemCoreClock = 66000000U;

static void aoci_clock_reset(void);
static uint32_t aoci_pll_start(uint32_t cfg_src_pre, uint32_t cfg_mul);
static void aoci_select_pll(void);

/*!
    \brief  remet l'horloge à son état de reset (HSI 8 MHz, PLL et quartz coupés)
*/
static void aoci_clock_reset(void)
{
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
    }
    RCU_CFG0 &= ~RCU_CFG0_SCS;                       /* HSI en horloge système */
    while(RCU_SCSS_IRC8M != (RCU_CFG0 & RCU_CFG0_SCSS)){
    }
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN | RCU_CTL_HXTALBPS);
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC |
                  RCU_CFG0_ADCPSC | RCU_CFG0_ADCPSC_2 | RCU_CFG0_CKOUT0SEL |
                  RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0 | RCU_CFG0_PLLMF |
                  RCU_CFG0_USBDPSC | RCU_CFG0_PLLMF_4);
    RCU_INT = 0x009F0000U;                           /* toutes interruptions RCU coupées */
}

/*!
    \brief  configure et démarre la PLL
    \param[in]  cfg_src_pre : RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0 (quartz/2) ou RCU_PLLSRC_IRC8M_DIV2
    \param[in]  cfg_mul     : RCU_PLL_MULxx
    \retval     1 si la PLL est verrouillée, 0 sinon
*/
static uint32_t aoci_pll_start(uint32_t cfg_src_pre, uint32_t cfg_mul)
{
    uint32_t timeout = AOCI_PLL_TIMEOUT * 100U;

    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0 | RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (cfg_src_pre | cfg_mul);
    RCU_CTL |= RCU_CTL_PLLEN;
    while((0U == (RCU_CTL & RCU_CTL_PLLSTB)) && (0U != timeout)){
        timeout--;
    }
    return (0U != (RCU_CTL & RCU_CTL_PLLSTB)) ? 1U : 0U;
}

/*!
    \brief  passe l'horloge système sur la PLL (prescalers déjà réglés)
*/
static void aoci_select_pll(void)
{
    uint32_t reg = RCU_CFG0;
    reg &= ~RCU_CFG0_SCS;
    reg |= RCU_CKSYSSRC_PLL;
    RCU_CFG0 = reg;
    while(RCU_SCSS_PLL != (RCU_CFG0 & RCU_CFG0_SCSS)){
    }
}

/*!
    \brief  initialisation de l'horloge ; appelée par le démarrage avant main()
*/
void SystemInit(void)
{
    uint32_t timeout = AOCI_HXTAL_TIMEOUT * 100U;
    uint32_t pll_ok = 0U;

    aoci_clock_reset();

    /* 2 états d'attente flash (jusqu'à 72 MHz), prélecture activée comme l'origine */
    FMC_WS = (FMC_WS & ~FMC_WS_WSCNT) | WS_WSCNT_2 | AOCI_FMC_WS_PFEN;

    /* prescalers : AHB /1, APB2 /1, APB1 /2 ; l'horloge ADC est réglée dans main() */
    RCU_CFG0 |= (RCU_AHB_CKSYS_DIV1 | RCU_APB2_CKAHB_DIV1 | RCU_APB1_CKAHB_DIV2);

#ifndef AOCI_TEST_NO_HXTAL   /* banc d'essai uniquement : simule un quartz absent pour exercer le repli HSI */
    /* tentative nominale : quartz 12 MHz / 2 x 11 = 66 MHz */
    RCU_CTL |= RCU_CTL_HXTALEN;
    while((0U == (RCU_CTL & RCU_CTL_HXTALSTB)) && (0U != timeout)){
        timeout--;
    }
    if(0U != (RCU_CTL & RCU_CTL_HXTALSTB)){
        pll_ok = aoci_pll_start(RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0, RCU_PLL_MUL11);
        if(0U == pll_ok){
            RCU_CTL &= ~RCU_CTL_PLLEN;
        }
    }
#endif

    /* repli : HSI/2 x 16 = 64 MHz */
    if(0U == pll_ok){
        RCU_CTL &= ~RCU_CTL_HXTALEN;
        pll_ok = aoci_pll_start(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL16);
    }

    if(0U != pll_ok){
        aoci_select_pll();
    }
    /* sinon : on reste sur le HSI 8 MHz ; SystemCoreClock le reflète */

    SystemCoreClockUpdate();

    nvic_vector_table_set(NVIC_VECTTAB_FLASH, AOCI_VECT_TAB_OFFSET);
}

/*!
    \brief  met à jour SystemCoreClock d'après les registres RCU (quartz 12 MHz)
*/
void SystemCoreClockUpdate(void)
{
    static const uint8_t ahb_exp[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
    uint32_t src_clk;
    uint32_t pllmf;

    switch(GET_BITS(RCU_CFG0, 2, 3)){
    case 1U:                                          /* quartz direct */
        src_clk = HXTAL_VALUE;
        break;
    case 2U:                                          /* PLL */
        if(RCU_PLLSRC_IRC8M_DIV2 == (RCU_CFG0 & RCU_CFG0_PLLSEL)){
            src_clk = IRC8M_VALUE / 2U;
        }else{
            src_clk = HXTAL_VALUE;
            if(RCU_CFG0_PREDV0 == (RCU_CFG0 & RCU_CFG0_PREDV0)){
                src_clk /= 2U;
            }
        }
        pllmf = GET_BITS(RCU_CFG0, 18, 21);
        if(0U != (RCU_CFG0 & RCU_CFG0_PLLMF_4)){
            pllmf |= 0x10U;
        }
        pllmf += (pllmf >= 15U) ? 1U : 2U;
        src_clk *= pllmf;
        break;
    default:                                          /* HSI */
        src_clk = IRC8M_VALUE;
        break;
    }
    SystemCoreClock = src_clk >> ahb_exp[GET_BITS(RCU_CFG0, 4, 7)];
}
